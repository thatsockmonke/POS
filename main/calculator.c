#include "cJSON.h"
#include "pos_appdata.h"
#include "pos_calc_tools.h"
#include "pos_calculator.h"
#include "pos_device.h"
#include "pos_files.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef POS_INSTALLER
typedef struct {
  const char *root;
  pos_calc_t calc;
  pos_view_t view;
  char expression[161], result[POS_CALC_TEXT], history[16][161];
  unsigned selection, history_count;
  bool dirty, readonly, keypad;
} calculator_t;
static const char *const keys[] = {
    "7", "8", "9", "/", "4", "5", "6", "*",    "1",     "2",    "3",   "-",
    "0", ".", "=", "+", "(", ")", "^", "sqrt", "Funcs", "Vars", "CAS", "Clear"};
static void history_save(calculator_t *s) {
  char path[POS_FS_PATH];
  if (pos_fs_join(path, s->root, "POS/Users/Owner") || pos_fs_mkdirs(path)) {
    pos_ui_notice("History not saved",
                  "Check SD space. Variables remain available.");
    return;
  }
  if (pos_fs_join(path, s->root, "POS/Users/Owner/calculator-history.jsonl"))
    return;
  cJSON *j = cJSON_CreateObject();
  if (j && cJSON_AddStringToObject(j, "expression", s->expression) &&
      cJSON_AddStringToObject(j, "result", s->result)) {
    char *text = cJSON_PrintUnformatted(j);
    if (!text || pos_fs_append_line(path, text))
      pos_ui_notice("History not saved",
                    "Check SD space. Variables remain available.");
    free(text);
  }
  cJSON_Delete(j);
}
static void history_load(calculator_t *s) {
  char path[POS_FS_PATH], line[1024];
  if (pos_fs_join(path, s->root, "POS/Users/Owner/calculator-history.jsonl"))
    return;
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  /* Seek a bounded tail; discard the first possibly incomplete record. */
  if (!fseek(f, 0, SEEK_END)) {
    long size = ftell(f);
    if (size > 16384) {
      fseek(f, size - 16384, SEEK_SET);
      while (fgets(line, sizeof(line), f) && !strchr(line, '\n')) {
      }
    } else
      rewind(f);
  }
  while (fgets(line, sizeof(line), f)) {
    if (!strchr(line, '\n')) {
      while (fgets(line, sizeof(line), f) && !strchr(line, '\n')) {
      }
      continue;
    }
    cJSON *j = pos_fs_json_safe(line, strlen(line))
                   ? cJSON_ParseWithOpts(line, NULL, true)
                   : NULL,
          *e = cJSON_GetObjectItemCaseSensitive(j, "expression");
    if (cJSON_IsString(e) && strlen(e->valuestring) < 161) {
      memmove(s->history[1], s->history[0], 15 * sizeof(s->history[0]));
      strcpy(s->history[0], e->valuestring);
      if (s->history_count < 16)
        ++s->history_count;
    }
    cJSON_Delete(j);
  }
  fclose(f);
}
static void worksheet(calculator_t *s, bool save, const char *relative) {
  char path[POS_FS_PATH], rel[241];
  if (save) {
    char name[65] = "Worksheet";
    if (!pos_ui_text("Save worksheet", "Name (new file in Documents)", name,
                     sizeof(name), false))
      return;
    if (!*name || strpbrk(name, "/\\:") || !strcmp(name, ".") ||
        !strcmp(name, "..")) {
      pos_ui_notice("Invalid name", "Use a simple filename.");
      return;
    }
    snprintf(rel, sizeof(rel), "POS/Users/Owner/Documents/%s.calc", name);
    relative = rel;
  }
  if (!relative || !pos_fs_user_path(relative) ||
      !pos_files_mutable(s->root, relative) ||
      pos_fs_join(path, s->root, relative)) {
    pos_ui_notice("Protected path", "Choose a user worksheet.");
    return;
  }
  if (save) {
    cJSON *j = cJSON_CreateObject(),
          *values = cJSON_CreateDoubleArray(s->calc.variables, 26);
    bool good = j && values && cJSON_AddItemToObject(j, "values", values);
    if (!good)
      cJSON_Delete(values);
    good = good && cJSON_AddNumberToObject(j, "format", 1) &&
           cJSON_AddNumberToObject(j, "mask", s->calc.assigned) &&
           cJSON_AddNumberToObject(j, "ans", s->calc.ans) &&
           cJSON_AddBoolToObject(j, "degrees", s->calc.degrees) &&
           cJSON_AddStringToObject(j, "expression", s->expression);
    char *text = good ? cJSON_PrintUnformatted(j) : NULL;
    char part[POS_FS_PATH];
    uint32_t crc = 0;
    int r =
        !text ||
        snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part) ||
        pos_fs_write_new(part, text, strlen(text)) ||
        pos_fs_crc_file(part, (uint32_t)strlen(text), &crc) ||
        crc != pos_crc32(0, text, strlen(text)) ||
        pos_fs_publish_new(part, path);
    free(text);
    cJSON_Delete(j);
    pos_ui_notice(
        r ? "Worksheet not saved" : "Worksheet saved",
        r ? "Existing files are preserved. Choose a new name or check SD."
          : relative);
  } else {
    char *text = calloc(1, 4097);
    FILE *f = text ? fopen(path, "rb") : NULL;
    bool bad = !f;
    if (f) {
      size_t n = fread(text, 1, 4096, f);
      bad = ferror(f) || fgetc(f) != EOF || memchr(text, 0, n);
      fclose(f);
    }
    cJSON *j = bad || !pos_fs_json_safe(text, strlen(text))
                   ? NULL
                   : cJSON_ParseWithOpts(text, NULL, true),
          *fmt = cJSON_GetObjectItemCaseSensitive(j, "format"),
          *v = cJSON_GetObjectItemCaseSensitive(j, "values"),
          *mask = cJSON_GetObjectItemCaseSensitive(j, "mask"),
          *ans = cJSON_GetObjectItemCaseSensitive(j, "ans"),
          *degrees = cJSON_GetObjectItemCaseSensitive(j, "degrees"),
          *e = cJSON_GetObjectItemCaseSensitive(j, "expression");
    bool good =
        cJSON_IsNumber(fmt) && fmt->valuedouble == 1 && cJSON_IsArray(v) &&
        cJSON_GetArraySize(v) == 26 && cJSON_IsNumber(mask) &&
        mask->valuedouble >= 0 && mask->valuedouble < 67108864 &&
        floor(mask->valuedouble) == mask->valuedouble && cJSON_IsNumber(ans) &&
        isfinite(ans->valuedouble) && cJSON_IsBool(degrees) &&
        cJSON_IsString(e) && strlen(e->valuestring) < 161;
    pos_calc_t calc = {0};
    for (unsigned i = 0; i < 26 && good; ++i) {
      cJSON *n = cJSON_GetArrayItem(v, i);
      good = cJSON_IsNumber(n) && isfinite(n->valuedouble);
      if (good)
        calc.variables[i] = n->valuedouble;
    }
    if (good) {
      calc.assigned = (uint32_t)mask->valuedouble;
      calc.ans = ans->valuedouble;
      calc.degrees = cJSON_IsTrue(degrees);
      s->calc = calc;
      strcpy(s->expression, e->valuestring);
      s->dirty = true;
      strcpy(s->result, "Worksheet opened; Enter evaluates");
    } else
      pos_ui_notice(
          "Worksheet preserved",
          "Not a supported POS worksheet. Current session unchanged.");
    cJSON_Delete(j);
    free(text);
  }
}
static void dot(uint8_t *pixels, int x, int y) {
  if (x >= 0 && x < 240 && y >= 0 && y < 88) {
    unsigned p = (unsigned)(y * 240 + x);
    pixels[p / 8] |= 1u << (p % 8);
  }
}
static void graph(calculator_t *s) {
  if (!*s->expression || strchr(s->expression, '=')) {
    pos_ui_notice("Graph",
                  "Enter a numeric function of x, e.g. sin(x) or x^2.");
    return;
  }
  uint8_t *pixels = calloc(1, 2640);
  if (!pixels) {
    pos_ui_notice("Graph", "Not enough memory");
    return;
  }
  double cx = 0, cy = 0, scale = 1;
  uint32_t generation = 0;
  bool redraw = true;
  while (!pos_home_pending()) {
    if (redraw) {
      memset(pixels, 0, 2640);
      for (int x = 0; x < 240; ++x) {
        double ay = 44 + cy * 12 / scale;
        if (ay >= 0 && ay < 88)
          dot(pixels, x, (int)ay);
      }
      double ax = 120 - cx * 12 / scale;
      if (ax >= 0 && ax < 240)
        for (int y = 0; y < 88; ++y)
          dot(pixels, (int)ax, y);
      int last = -1000;
      for (int x = 0; x < 240; ++x) {
        double value;
        int y = -1000;
        if (!pos_calc_sample(&s->calc, s->expression,
                             cx + (x - 120) * scale / 12, &value)) {
          double py = 44 - (value - cy) * 12 / scale;
          if (py >= 0 && py < 88)
            y = (int)py;
        }
        if (y >= 0) {
          dot(pixels, x, y);
          if (last >= 0 && abs(last - y) < 20)
            for (int r = last; r != y; r += r < y ? 1 : -1)
              dot(pixels, x, r);
        }
        last = y;
      }
      pos_view_clear(&s->view, "Graph y=f(x)");
      pos_ui_style(&s->view);
      s->view.map_pixels = pixels;
      s->view.map_generation = ++generation;
      pos_view_line(&s->view, 13, "x %.4g y %.4g span %.4g", cx, cy,
                    scale * 20);
      pos_view_line(&s->view, 14, "Arrows:pan +/-:zoom 0:reset");
      pos_view_line(&s->view, 15, "Esc:Calculator");
      pos_device_show(&s->view);
      redraw = false;
    }
    pos_event_t e;
    if (!pos_device_event(&e, 100) || !e.pressed)
      continue;
    int k = pos_navigation(&e);
    if (k == POS_KEY_ESCAPE || k == POS_KEY_HOME)
      break;
    if (k == POS_KEY_LEFT)
      cx -= scale * 2;
    if (k == POS_KEY_RIGHT)
      cx += scale * 2;
    if (k == POS_KEY_UP)
      cy += scale * 2;
    if (k == POS_KEY_DOWN)
      cy -= scale * 2;
    if (e.key == '+' || e.key == '=')
      scale = fmax(1e-8, scale / 1.5);
    if (e.key == '-')
      scale = fmin(1e8, scale * 1.5);
    if (e.key == '0') {
      cx = cy = 0;
      scale = 1;
    }
    redraw = true;
  }
  s->view.map_pixels = NULL;
  pos_ui_message("Calculator", "");
  free(pixels);
}

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
  history_save(s);
  if (!s->history_count || strcmp(s->history[0], s->expression)) {
    memmove(s->history[1], s->history[0], 15 * sizeof(s->history[0]));
    strcpy(s->history[0], s->expression);
    if (s->history_count < 16)
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
    pos_ui_help(s->root, "CAS help", "cas-help.txt");
    return;
  }
  calculate(s, (pos_calc_action_t)(POS_CALC_SOLVE + i));
}
static void advanced(calculator_t *s) {
  const char *items[] = {"Exact fraction arithmetic",
                         "Linear system (2..4 unknowns)",
                         "Matrices / vectors",
                         "Unit conversion",
                         "Programmer / bit operations",
                         "Graph y=f(x)",
                         "Save named worksheet",
                         "Open worksheet",
                         "Back"};
  int choice = pos_ui_menu("Calculator modes", items, 9);
  char a[161] = "", b[161] = "";
  if (choice == 0) {
    if (pos_ui_text("Exact fractions", "+, -, *, /, ^, decimal, parentheses",
                    s->expression, sizeof(s->expression), false))
      pos_calc_exact(s->expression, s->result);
  }
  if (choice == 1) {
    if (pos_ui_text("Linear system", "2*x+y=3;x-y=0 (2..4 equations)", a,
                    sizeof(a), false))
      pos_calc_system(&s->calc, a, s->result);
  }
  if (choice == 2) {
    const char *ops[] = {"Add",         "Subtract", "Multiply",
                         "Determinant", "Inverse",  "Vector dot product"};
    const char *names[] = {"add", "sub", "mul", "det", "inv", "dot"};
    int op = pos_ui_menu("Matrix operation", ops, 6);
    if (op >= 0 &&
        pos_ui_text("Matrix A", "Rows separated by ; values by comma", a,
                    sizeof(a), false) &&
        ((op == 3 || op == 4) ||
         pos_ui_text("Matrix B", "Vector: one comma-separated row", b,
                     sizeof(b), false)))
      pos_calc_matrix(names[op], a, b, s->result);
  }
  if (choice == 3) {
    char from[12] = "m", to[12] = "ft";
    if (pos_ui_text("Value", "Numeric expression (saved vars allowed)", a,
                    sizeof(a), false) &&
        pos_ui_text("From unit", "m km mi ft in kg g lb s min h C F K", from,
                    sizeof(from), false) &&
        pos_ui_text("To unit", "Also m/s km/h mph m2 km2 acre L mL gal", to,
                    sizeof(to), false)) {
      pos_calc_t copy = s->calc;
      char result[POS_CALC_TEXT];
      double v;
      if (pos_calc_run(&copy, a, POS_CALC_EVALUATE, result) ||
          pos_calc_units(copy.ans, from, to, &v))
        strcpy(s->result, "Unknown or incompatible units");
      else
        snprintf(s->result, sizeof(s->result), "%.12g %s = %.12g %s", copy.ans,
                 from, v, to);
    }
  }
  if (choice == 4) {
    const char *widths[] = {"8 bit", "16 bit", "32 bit", "64 bit"};
    unsigned bits[] = {8, 16, 32, 64};
    int width = pos_ui_menu("Unsigned programmer width", widths, 4);
    if (width >= 0 &&
        pos_ui_text("Bit expression", "Hex 0xFF / binary 0b11; & | ^ << >> ~",
                    a, sizeof(a), false))
      pos_calc_bits(a, bits[width], s->result);
  }
  if (choice == 5)
    graph(s);
  if (choice == 6)
    worksheet(s, true, NULL);
  if (choice == 7) {
    char relative[241];
    if (pos_ui_pick_file_from(s->root, "Calculator worksheet", "Documents",
                              relative))
      worksheet(s, false, relative);
  }
  if (choice >= 0 && choice < 5)
    pos_ui_notice("Result", s->result);
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
                               "More modes / worksheets",
                               "Back"};
  int i = pos_ui_menu("Calculator tools", items, 11);
  if (i == 9)
    advanced(s);
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
    const char *history[16];
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
    pos_ui_help(s->root, "Calculator help", "calculator-help.txt");
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
void pos_ui_calculator_open(const char *root, const char *relative) {
#ifndef POS_INSTALLER
  calculator_t *s = calloc(1, sizeof(*s));
  if (!s) {
    pos_ui_notice("Calculator", "Not enough memory");
    return;
  }
  s->root = root;
  s->selection = 14;
  s->calc.degrees = true;
  strcpy(s->result, "Enter an expression");
  if (load_save(root, s, false))
    pos_ui_notice(
        "Calculator save preserved",
        "Existing save could not be read. This session will not overwrite it.");
  history_load(s);
  if (relative)
    worksheet(s, false, relative);
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
  (void)relative;
#endif
}

void pos_ui_calculator(const char *root) { pos_ui_calculator_open(root, NULL); }
