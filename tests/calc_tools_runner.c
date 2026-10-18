#include "pos_calc_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  char out[POS_CALC_TEXT] = "";
  int r = -1;
  pos_calc_t c = {0};
  if (!strcmp(argv[1], "exact"))
    r = pos_calc_exact(argv[2], out);
  if (!strcmp(argv[1], "system"))
    r = pos_calc_system(&c, argv[2], out);
  if (!strcmp(argv[1], "bits") && argc == 4)
    r = pos_calc_bits(argv[2], strtoul(argv[3], NULL, 10), out);
  if (!strcmp(argv[1], "matrix") && argc == 5)
    r = pos_calc_matrix(argv[2], argv[3], argv[4], out);
  if (!strcmp(argv[1], "unit") && argc == 5) {
    double y;
    r = pos_calc_units(strtod(argv[2], NULL), argv[3], argv[4], &y);
    if (!r)
      snprintf(out, sizeof(out), "%.15g", y);
  }
  printf("%d\t%s\n", r, out);
  return 0;
}
