#include <stdlib.h>
#include "type.h"
#include "instr.h"
#include "import.h"

SFUN(coverage_int) {
  puts("test");
  *(m_int*)RETURN = *(m_int*)MEM(SZ_INT);
}

IMPORT {
  CHECK_BB(gwi_func_ini(gwi, "int", "test", coverage_int))
  CHECK_BB(gwi_func_arg(gwi, "int", "i"))
  CHECK_BB(gwi_func_end(gwi, 0))
  return 1;
}
