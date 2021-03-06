#include "type.h"
#include "import.h"
#include "object.h"

static m_int o_map_key;
static m_int o_map_value;
#define MAP_KEY(a) *((M_Object*)(a->data + o_map_key))
#define MAP_VAL(a) *((M_Object*)(a->data + o_map_value))
static CTOR(class_template_ctor) {
//exit(2);
  /*char* name = strdup(o->type_ref->name);*/
  /*char* tmp = strsep(&name, "@");*/
  /*char* name1 = strsep(&name, "@");*/
/*Type t1 = nspc_lookup_type1(o->type_ref->info->parent, insert_symbol(name1));*/
  /*Type t2 = nspc_lookup_type0(shred->vm_ref->emit->env->curr, insert_symbol(name));*/
/*free(tmp);*/
/**(M_Object*)(o->data) = new_M_Array(t1->size, 0, t1->array_depth);*/
  /**(M_Object*)(o->data + SZ_INT) = new_M_Array(t2->size, 0, t2->array_depth);*/
}

static MFUN(class_template_set) {

}

IMPORT
{
  Type t_class_template;
  const m_str list[2] = { "A", "B" };
  gwi_tmpl_ini(gwi, 2, list);
  CHECK_OB((t_class_template = gwi_mk_type(gwi, "ClassTemplate", SZ_INT, t_object)))
  CHECK_BB(gwi_class_ini(gwi, t_class_template, class_template_ctor, NULL))
  gwi_tmpl_end(gwi);
  CHECK_BB(gwi_item_ini(gwi, "A[]", "key"))
    CHECK_BB((o_map_key = gwi_item_end(gwi, ae_flag_member | ae_flag_template, NULL)))
    CHECK_BB(gwi_item_ini(gwi, "B[]", "value"))
    CHECK_BB((o_map_value = gwi_item_end(gwi, ae_flag_member, NULL)))


    /*gwi_func_ini(gwi, "B", "set", class_template_set);*/
    /*gwi_func_end(gwi, ae_flag_member);*/
  CHECK_BB(gwi_class_end(gwi))
  return 1;
}
