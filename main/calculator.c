#include "cJSON.h"
#include "pos_appdata.h"
#include "pos_calculator.h"
#include "pos_device.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef POS_INSTALLER
typedef struct {
  pos_calc_t calc;
  pos_view_t view;
  char expression[161], result[POS_CALC_TEXT], history[6][161];
  unsigned selection, history_count;
  bool dirty, readonly, keypad;
} calculator_t;
static const char *const keys[] = {
    "7", "8", "9", "/", "4", "5", "6", "*",    "1",     "2",    "3",   "-",
    "0", ".", "=", "+", "(", ")", "^", "sqrt", "Funcs", "Vars", "CAS", "Clear"};
static void append(calculator_t *s, const char *text) {
  size_t n = strlen(s->expression), len = strlen(text);
  if (n + len < sizeof(s->expression)) {
    memcpy(s->expression + n, text, len + 1);
  } else
    snprintf(s->result, sizeof(s->result), "Expression limit: 160 characters");
}
static void calculate(calculator_t *s, pos_calc_action_t action) {
  if (!*s->expression)
    return;
  if (!pos_calc_run(&s->calc, s->expression, action, s->result))
    s->dirty = true;
  if (!s->history_count || strcmp(s->history[0], s->expression)) {
    memmove(s->history[1], s->history[0], 5 * sizeof(s->history[0]));
    strcpy(s->history[0], s->expression);
    if (s->history_count < 6)
      ++s->history_count;
  }
}
static void functions(calculator_t *s) {
  static const char *const groups[] = {
      "Trig / inverse trig",     "Hyperbolic / inverse",
      "Logs / roots / powers",   "Rounding / magnitude",
      "Combinations / integers", "Statistics / geometry",
      "Constants / shortcuts",   "Back"};
  int group = pos_ui_menu("Scientific functions", groups, 8);
  if (group < 0 || group == 7)
    return;
  static const unsigned indices[][8] = {
      {0, 1, 2, 3, 4, 5},           {6, 7, 8, 9, 10, 11},
      {12, 13, 14, 15, 16, 32, 33}, {17, 18, 19, 20, 21},
      {22, 23, 24, 25, 26, 27},     {28, 29, 30, 31, 36, 37}};
  static const unsigned counts[] = {6, 6, 7, 5, 6, 6};
  if (group == 6) {
    const char *const shortcuts[] = {"pi", "e",     "tau", "ans", "^2",
                                     "^3", "^(-1)", "!",   "%",   "1/"};
    int i = pos_ui_menu("Insert", shortcuts, 10);
    if (i >= 0)
      append(s, shortcuts[i]);
    return;
  }
  const char *items[8];
  for (unsigned i = 0; i < counts[group]; ++i)
    items[i] = pos_calc_functions[indices[group][i]];
  int i = pos_ui_menu("Insert function", items, counts[group]);
  if (i >= 0) {
    append(s, items[i]);
    append(s, "(");
  }
}
static void variables(calculator_t *s) {
  char labels[26][40];
  const char *items[26];
  for (unsigned i = 0; i < 26; ++i) {
    char n[32];
    if (i == 4)
      strcpy(n, "constant");
    else if (s->calc.assigned & (1u << i))
      pos_calc_number(s->calc.variables[i], n, sizeof(n));
    else
      strcpy(n, i == 23 ? "symbolic" : "unset");
    snprintf(labels[i], sizeof(labels[i]), "%c = %s", 'a' + i, n);
    items[i] = labels[i];
  }
  int i = pos_ui_menu("Variables (a-z)", items, 26);
  if (i < 0)
    return;
  if (i == 4) {
    pos_ui_notice("Euler's number", "e is a constant. Use another variable.");
    return;
  }
  char value[100] = "", label[64];
  if (s->calc.assigned & (1u << i))
    snprintf(value, sizeof(value), "%.17g", s->calc.variables[i]);
  snprintf(label, sizeof(label), "%c = (blank clears variable)", 'a' + i);
  if (!pos_ui_text("Set variable", label, value, sizeof(value), false))
    return;
  if (!*value) {
    s->calc.assigned &= ~(1u << i);
    s->dirty = true;
    strcpy(s->result, "Variable cleared");
    return;
  }
  char assignment[110];
  snprintf(assignment, sizeof(assignment), "%c=%s", 'a' + i, value);
  if (!pos_calc_run(&s->calc, assignment, POS_CALC_EVALUATE, s->result))
    s->dirty = true;
}
static void cas(calculator_t *s) {
  const char *const items[] = {
      "Solve equation for x",  "Simplify / expand polynomial",
      "Differentiate in x",    "Integrate in x",
      "CAS examples / limits", "Back"};
  int i = pos_ui_menu("Computer algebra", items, 6);
  if (i < 0 || i == 5)
    return;
  if (i == 4) {
    pos_ui_notice(
        "CAS help",
        "x is symbolic in CAS, even if assigned. Examples: (x+1)^3; "
        "x^2-5*x+6=0; 2*x+3=7. Degree up to 8; solve degree 1/2, including "
        "complex roots. Derivatives/integrals are polynomial. Numeric "
        "functions need numeric arguments. Calculations use double precision, "
        "not unlimited exact algebra. Use * between named variables.");
    return;
  }
  calculate(s, (pos_calc_action_t)(POS_CALC_SOLVE + i));
}
static void tools(calculator_t *s) {
  const char *const items[] = {"Edit expression",
                               "Scientific functions",
                               "Variables",
                               "Computer algebra",
                               "Toggle DEG / RAD",
                               "Recent expressions",
                               "View full answer",
                               "Calculator help",
                               "Toggle typing / keypad",
                               "Back"};
  int i = pos_ui_menu("Calculator tools", items, 10);
  if (i == 0 && pos_ui_text("Expression", "Enter evaluates; Esc cancels",
                            s->expression, sizeof(s->expression), false))
    calculate(s, POS_CALC_EVALUATE);
  if (i == 1)
    functions(s);
  if (i == 2)
    variables(s);
  if (i == 3)
    cas(s);
  if (i == 4) {
    s->calc.degrees = !s->calc.degrees;
    s->dirty = true;
  }
  if (i == 5) {
    const char *history[6];
    for (unsigned j = 0; j < s->history_count; ++j)
      history[j] = s->history[j];
    if (s->history_count) {
      int j = pos_ui_menu("Recent expressions", history, s->history_count);
      if (j >= 0)
        strcpy(s->expression, s->history[j]);
    }
  }
  if (i == 6)
    pos_ui_notice("Answer", s->result);
  if (i == 8)
    s->keypad = !s->keypad;
  if (i == 7)
    pos_ui_notice(
        "Calculator help",
        "Type an expression and press Enter. Arrows select buttons; Enter "
        "presses the selected button. Backspace deletes. Tab opens tools. "
        "Examples: 1/3+1/6, sqrt(2), 2^3, sin(30), a=12, a/4. DEG/RAD affects "
        "trig. Use commas for functions such as ncr(5,2), root(27,3), "
        "mean(2,4,6), std(2,4,6). std uses the sample formula. Vars are saved "
        "on SD. CAS works with polynomials in x; blank x in Vars restores "
        "symbolic evaluation. Esc or global Home closes.");
}
static int load_save(const char *root, calculator_t *s, bool save) {
  pos_appdata_t *data = calloc(1, sizeof(*data));
  if (!data)
    return -1;
  pos_error_t e = {0};
  int status = pos_appdata_load(root, "calculator", 1, NULL, NULL, data, &e);
  if (status < 0) {
    free(data);
    s->readonly = true;
    return -1;
  }
  if (!save && status == 0) {
    cJSON *j = cJSON_Parse(data->json),
          *v = cJSON_GetObjectItemCaseSensitive(j, "values"),
          *mask = cJSON_GetObjectItemCaseSensitive(j, "mask"),
          *degrees = cJSON_GetObjectItemCaseSensitive(j, "degrees"),
          *ans = cJSON_GetObjectItemCaseSensitive(j, "ans");
    bool valid = cJSON_IsArray(v) && cJSON_GetArraySize(v) == 26 &&
                 cJSON_IsNumber(mask) && mask->valuedouble >= 0 &&
                 mask->valuedouble < 67108864 &&
                 mask->valuedouble == floor(mask->valuedouble) &&
                 cJSON_IsBool(degrees) && cJSON_IsNumber(ans) &&
                 isfinite(ans->valuedouble);
    pos_calc_t calc = {0};
    for (unsigned i = 0; i < 26 && valid; ++i) {
      cJSON *n = cJSON_GetArrayItem(v, i);
      valid = cJSON_IsNumber(n) && isfinite(n->valuedouble);
      if (valid)
        calc.variables[i] = n->valuedouble;
    }
    if (valid) {
      calc.assigned = (uint32_t)mask->valuedouble;
      calc.degrees = cJSON_IsTrue(degrees);
      calc.ans = ans->valuedouble;
      s->calc = calc;
    } else {
      s->readonly = true;
      status = -1;
    }
    cJSON_Delete(j);
  }
  if (save && !s->readonly) {
    cJSON *j = cJSON_CreateObject();
    if (!j) {
      free(data);
      return -1;
    }
    cJSON *values = cJSON_CreateDoubleArray(s->calc.variables, 26);
    bool good = values && cJSON_AddItemToObject(j, "values", values);
    if (!good)
      cJSON_Delete(values);
    good = good && cJSON_AddNumberToObject(j, "mask", s->calc.assigned) &&
           cJSON_AddBoolToObject(j, "degrees", s->calc.degrees) &&
           cJSON_AddNumberToObject(j, "ans", s->calc.ans) &&
           cJSON_PrintPreallocated(j, data->json, sizeof(data->json), false);
    cJSON_Delete(j);
    data->schema = 1;
    status = good ? pos_appdata_save(root, "calculator", data, &e) : -1;
  }
  free(data);
  return status < 0 ? -1 : 0;
}
static void draw(calculator_t *s) {
  pos_view_t *v = &s->view;
  pos_view_clear(v, s->calc.degrees ? "Calculator / DEG" : "Calculator / RAD");
  pos_ui_style(v);
  v->black_background = true;
  size_t n = strlen(s->expression), start = n > 60 ? n - 60 : 0;
  pos_view_line(v, 1, "%.30s", s->expression + start);
  if (n - start > 30)
    pos_view_line(v, 2, "%.30s", s->expression + start + 30);
  for (unsigned row = 0; row < 3; ++row) {
    size_t at = row * 30;
    if (strlen(s->result) > at)
      pos_view_line(v, 4 + row, "%.30s", s->result + at);
  }
  for (unsigned row = 0; row < 6; ++row) {
    char text[32] = "";
    for (unsigned col = 0; col < 4; ++col) {
      unsigned i = row * 4 + col;
      char cell[8];
      snprintf(cell, sizeof(cell), i == s->selection ? "[%-5s]" : " %-5s ",
               keys[i]);
      strcat(text, cell);
    }
    pos_view_line(v, 8 + row, "%s", text);
  }
  pos_view_line(v, 14, "%s",
                s->keypad ? "Keypad: arrows / Enter:press"
                          : "Typing: Enter=evaluate");
  pos_view_line(v, 15, "Ctrl+K:mode Tab:tools Esc:exit");
  pos_device_show(v);
}
#endif
void pos_ui_calculator(const char *root) {
#ifndef POS_INSTALLER
  calculator_t *s = calloc(1, sizeof(*s));
  if (!s) {
    pos_ui_notice("Calculator", "Not enough memory");
    return;
  }
  s->selection = 14;
  s->calc.degrees = true;
  strcpy(s->result, "Enter an expression");
  if (load_save(root, s, false))
    pos_ui_notice(
        "Calculator save preserved",
        "Existing save could not be read. This session will not overwrite it.");
  bool redraw = true;
  while (!pos_home_pending()) {
    if (redraw) {
      draw(s);
      redraw = false;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 100) || !event.pressed)
      continue;
    redraw = true;
    if ((event.modifiers & POS_MOD_CTRL) &&
        (event.key == 'k' || event.key == 'K')) {
      s->keypad = !s->keypad;
      continue;
    }
    if (event.key >= POS_KEY_UP && event.key <= POS_KEY_RIGHT)
      s->keypad = true;
    int key = s->keypad ? pos_navigation(&event) : event.key;
    if (key == POS_KEY_ESCAPE || key == POS_KEY_HOME)
      break;
    if (key == POS_KEY_LEFT)
      s->selection = (s->selection / 4) * 4 + (s->selection + 3) % 4;
    else if (key == POS_KEY_RIGHT)
      s->selection = (s->selection / 4) * 4 + (s->selection + 1) % 4;
    else if (key == POS_KEY_UP)
      s->selection = (s->selection + 20) % 24;
    else if (key == POS_KEY_DOWN)
      s->selection = (s->selection + 4) % 24;
    else if (key == POS_KEY_BACKSPACE || key == POS_KEY_DELETE) {
      size_t n = strlen(s->expression);
      if (n)
        s->expression[n - 1] = 0;
      s->selection = 14;
    } else if (key == '\t')
      tools(s);
    else if (key == POS_KEY_ENTER) {
      if (!s->keypad || s->selection == 14)
        calculate(s, POS_CALC_EVALUATE);
      else if (s->selection == 19) {
        append(s, "sqrt(");
      } else if (s->selection == 20)
        functions(s);
      else if (s->selection == 21)
        variables(s);
      else if (s->selection == 22)
        cas(s);
      else if (s->selection == 23) {
        *s->expression = 0;
        *s->result = 0;
      } else
        append(s, keys[s->selection]);
    } else if (key >= 32 && key < 127 &&
               !(event.modifiers & (POS_MOD_CTRL | POS_MOD_ALT))) {
      char text[2] = {(char)key, 0};
      append(s, text);
      s->keypad = false;
      s->selection = 14;
    }
  }
  if (s->dirty && !s->readonly && load_save(root, s, true))
    pos_ui_notice("Calculator save failed",
                  "Existing saved variables are retained. This session's "
                  "changes were not saved.");
  pos_ui_message("Launcher", "");
  free(s);
#else
  (void)root;
#endif
}
