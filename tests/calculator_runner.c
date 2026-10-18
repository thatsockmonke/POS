#include "pos_calculator.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  pos_calc_t c = {0};
  char result[POS_CALC_TEXT];
  for (int i = 1; i + 1 < argc; i += 2) {
    pos_calc_action_t action = POS_CALC_EVALUATE;
    if (!strcmp(argv[i], "solve"))
      action = POS_CALC_SOLVE;
    else if (!strcmp(argv[i], "expand"))
      action = POS_CALC_EXPAND;
    else if (!strcmp(argv[i], "diff"))
      action = POS_CALC_DIFFERENTIATE;
    else if (!strcmp(argv[i], "integrate"))
      action = POS_CALC_INTEGRATE;
    else if (!strcmp(argv[i], "deg")) {
      c.degrees = true;
    }
    int status = pos_calc_run(&c, argv[i + 1], action, result);
    printf("%d\t%s\n", status, result);
  }
  return 0;
}
