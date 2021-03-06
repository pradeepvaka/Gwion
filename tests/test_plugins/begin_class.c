#include "defs.h"
#include "type.h"
#include "import.h"

MFUN(test_mfun){}
IMPORT
{
  Type t_invalid_var_name;
  CHECK_OB((t_invalid_var_name = gwi_mk_type(gwi, "invalid_var_name", SZ_INT, t_object)))
  CHECK_BB(gwi_class_ini(gwi, t_invalid_var_name, NULL, NULL))
  CHECK_BB(gwi_class_ini(gwi, t_invalid_var_name, NULL, NULL))
  return 1;
}
