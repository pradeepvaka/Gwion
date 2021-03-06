#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <inttypes.h>
#include "defs.h"
#include "err_msg.h"
#include "type.h"
#include "value.h"
#include "func.h"
#include "import.h"
#include "traverse.h"
#include "plug.h"

#define OP_RET(a, b)\
  Type op_ret = op_check(env, &opi);\
  if(!op_ret)\
    CHECK_BO(err_msg(TYPE_, a->pos, "in %s expression", b))\
  return op_ret;

ANN Type   check_exp(const Env env, Exp exp);
ANN static m_bool check_stmt(const Env env, const Stmt stmt);
ANN static m_bool check_stmt_list(const Env env, Stmt_List list);
ANN m_bool check_class_def(const Env env, const Class_Def class_def);

ANN m_bool check_exp_array_subscripts(Exp exp) { GWDEBUG_EXE
  do if(isa(exp->type, t_int) < 0)
      CHECK_BB(err_msg(TYPE_, exp->pos, "incompatible array subscript type '%s'...", exp->type->name))
  while((exp = exp->next));
  return 1;
}

ANN static m_bool check_exp_decl_template(const Env env, const Exp_Decl* decl) { GWDEBUG_EXE
  if(!GET_FLAG(decl->m_type, ae_flag_check)) {
    CHECK_BB(template_push_types(env, decl->base->tmpl->list.list, decl->type->types))
    CHECK_BB(check_class_def(env, decl->m_type->def))
    nspc_pop_type(env->curr);
  }
  return 1;
}

ANN static m_bool check_exp_decl_parent(const Env env, const Var_Decl var) { GWDEBUG_EXE
  const Value value = find_value(env->class_def->parent, var->xid);
  if(value)
    CHECK_BB(err_msg(TYPE_, var->pos,
           "in class '%s': '%s' has already been defined in parent class '%s'...",
           env->class_def->name, s_name(var->xid), value->owner_class->name))
  return 1;
}

ANN static m_bool check_exp_decl_array(const Env env, const Exp array) { GWDEBUG_EXE
  CHECK_OB(check_exp(env, array))
  return check_exp_array_subscripts(array);
}

ANN static inline m_bool check_exp_decl_member(const Nspc nspc, const Value v) { GWDEBUG_EXE
  v->offset = nspc->offset;
  nspc->offset += v->m_type->size;
  return 1;
}

ANN static m_bool check_exp_decl_static(const Env env , const Value v, const m_uint pos) { GWDEBUG_EXE
  Nspc nspc = env->curr;

  if(!env->class_def || env->class_scope > 0)
    CHECK_BB(err_msg(TYPE_, pos,
          "static variables must be declared at class scope..."))
  SET_FLAG(v, ae_flag_static);
  v->offset = nspc->class_data_size;
  nspc->class_data_size += v->m_type->size;
  return 1;
}

ANN static m_bool check_exp_decl_valid(const Env env, const Value v, const Symbol xid) { GWDEBUG_EXE
  SET_FLAG(v, ae_flag_checked);
  if(!env->class_def || env->class_scope)
    nspc_add_value(env->curr, xid, v);
  return 1;
}

ANN Type check_exp_decl(const Env env, const Exp_Decl* decl) { GWDEBUG_EXE
  Var_Decl_List list = decl->list;
  if(GET_FLAG(decl->m_type , ae_flag_template))
    CHECK_BO(check_exp_decl_template(env, decl))
  while(list) {
    Var_Decl var = list->self;
    Value value = var->value;
    if(env->class_def && !env->class_scope && env->class_def->parent)
      CHECK_BO(check_exp_decl_parent(env, var))
    if(var->array && var->array->exp_list)
      CHECK_BO(check_exp_decl_array(env, var->array->exp_list))
    if(GET_FLAG(value, ae_flag_member))
      CHECK_BO(check_exp_decl_member(env->curr, value))
    else if(GET_FLAG(decl->type, ae_flag_static))
      CHECK_BO(check_exp_decl_static(env, value, var->pos))
    CHECK_BO(check_exp_decl_valid(env, value, var->xid))
    list = list->next;
  }
  return decl->m_type;
}

ANN static m_bool check_exp_prim_array_inner(const Type t, Type type, const Exp e) { GWDEBUG_EXE
  const Type common = find_common_anc(t, type);
  if(common)
    type = common;
  else {
    if(isa(t, t_int) > 0 && isa(type, t_float) > 0) {
      e->cast_to = type;
      return 1;
    } else
      CHECK_BB(err_msg(TYPE_, e->pos, "array init [...] contains incompatible types..."))
  }
  return 1;
}

ANN static Type check_exp_prim_array_match(Exp e) { GWDEBUG_EXE
  Type t, type = NULL;
  do {
    t = e->type;
    if(!type)
      type = t;
    else
      CHECK_BO(check_exp_prim_array_inner(t, type, e))
  } while((e = e->next));
  return array_type(type->array_depth ? array_base(type) : type, type->array_depth + 1);
}

ANN static Type check_exp_prim_array(const Env env, const Array_Sub array) { GWDEBUG_EXE
  Exp e = array->exp_list;
  if(!e)
    CHECK_BO(err_msg(TYPE_, array->pos, "must provide values/expressions for array [...]"))
  CHECK_OO(check_exp(env, e))
  return (array->type = check_exp_prim_array_match(e));
}

ANN static Value check_non_res_value(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  m_str str = s_name(primary->d.var);
  Value v = nspc_lookup_value1(env->curr, primary->d.var);
  if(!v)
    v = env->class_def ? find_value(env->class_def, primary->d.var) : NULL;
  if(v && env->class_def && env->func && GET_FLAG(env->func->def, ae_flag_static) &&
    GET_FLAG(v, ae_flag_member) && !GET_FLAG(v, ae_flag_static))
        CHECK_BO(err_msg(TYPE_, primary->pos,
                         "non-static member '%s' used from static function...", str))
  return v;
}

ANN static Type check_exp_prim_id_non_res(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  const Value v = check_non_res_value(env, primary);
  if(!v || !GET_FLAG(v, ae_flag_checked))
    CHECK_BO(err_msg(TYPE_, primary->pos, "variable %s not legit at this point.",
          s_name(primary->d.var)))
  ((Exp_Primary*)primary)->value = v;
  if(GET_FLAG(v, ae_flag_const))
    primary->self->meta = ae_meta_value;
  return v->m_type;
}

ANN static Type check_exp_prim_this(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  if(!env->class_def)
    CHECK_BO(err_msg(TYPE_, primary->pos, "keyword 'this' can be used only inside class definition..."))
  if(env->func && !GET_FLAG(env->func, ae_flag_member))
    CHECK_BO(err_msg(TYPE_, primary->pos, "keyword 'this' cannot be used inside static functions..."))
  primary->self->meta = ae_meta_value;
  return env->class_def;
}

ANN static inline Type check_exp_prim_me(const Exp_Primary* primary) { GWDEBUG_EXE
  primary->self->meta = ae_meta_value;
  return t_shred;
}

ANN static inline Type check_exp_prim_now(const Exp_Primary* primary) { GWDEBUG_EXE
  primary->self->meta = ae_meta_var;
  return t_now;
}

ANN static Type check_exp_prim_id2(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  const m_str str = s_name(primary->d.var);

  if(!strcmp(str, "true") || !strcmp(str, "false") || !strcmp(str, "maybe")) {
    primary->self->meta = ae_meta_value;
    return t_int;
  } else
    return check_exp_prim_id_non_res(env, primary);
}

ANN static Type check_exp_prim_id1(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  const m_str str = s_name(primary->d.var);

  if(!strcmp(str, "NULL") || !strcmp(str, "null")) {
    primary->self->meta = ae_meta_value;
    return t_null;
  } else
    return check_exp_prim_id2(env, primary);
}

ANN static Type check_exp_prim_id(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  const m_str str = s_name(primary->d.var);

  if(!strcmp(str, "this"))
    return check_exp_prim_this(env, primary);
  else if(!strcmp(str, "me"))
    return check_exp_prim_me(primary);
  else if(!strcmp(str, "now"))
    return check_exp_prim_now(primary);
  else
    return check_exp_prim_id1(env, primary);
}

ANN static m_bool vec_value(const Env env, Exp e, const m_str s) {
  int count = 1;
  CHECK_OB(check_exp(env, e))
  do {
    Type t = e->type;
    if(isa(t, t_int) > 0) e->cast_to = t_float;
    else if(isa(t, t_float) < 0)
      CHECK_BB(err_msg(TYPE_, e->pos,
            "invalid type '%s' in %s value #%d...\n"
            "    (must be of type 'int' or 'float')", t->name, s, count))
    count++;
  } while((e = e->next));
  return 1;
}

struct VecInfo {
  Type   t;
  m_str  s;
  m_uint n;
};

ANN static void vec_info(const ae_Exp_Primary_Type t, struct VecInfo* v) {
  if(t == ae_primary_vec) {
    v->t = v->n == 4 ? t_vec4 : t_vec3;
    v->n = 4;
    v->s = "vector";
  } else if(t == ae_primary_complex) {
    v->s = "complex";
    v->t = t_complex;
    v->n = 2;
  } else {
    v->s = "polar";
    v->t = t_polar;
    v->n = 2;
  }
}

ANN static Type check_exp_prim_vec(const Env env, const Vec* vec, const ae_Exp_Primary_Type t) { GWDEBUG_EXE
  struct VecInfo info = { NULL, NULL, vec->dim };
  vec_info(t, &info);
  if(vec->dim > info.n)
    CHECK_BO(err_msg(TYPE_, vec->pos,
          "extraneous component of %s value...", info.s))
  CHECK_BO(vec_value(env, vec->exp, info.s))
  return info.t;
}

ANN static Type check_exp_prim_gack(const Env env, const Exp e) { GWDEBUG_EXE
  if(e->exp_type == ae_exp_decl)
    CHECK_BO(err_msg(TYPE_, e->pos, "cannot use <<< >>> on variable declarations...\n"))
  CHECK_OO((check_exp(env, e)))
  return t_gack;
}

ANN static Type check_exp_primary(const Env env, const Exp_Primary* primary) { GWDEBUG_EXE
  Type t = NULL;
  switch(primary->primary_type) {
    case ae_primary_id:
      t = check_exp_prim_id(env, primary);
      break;
    case ae_primary_num:
      t = t_int;
      break;
    case ae_primary_float:
      t = t_float;
      break;
    case ae_primary_complex:
    case ae_primary_polar:
    case ae_primary_vec:
      t = check_exp_prim_vec(env, &primary->d.vec, primary->primary_type);
      break;
    case ae_primary_nil:
      t = t_void;
      break;
    case ae_primary_str:
      t = t_string;
      break;
    case ae_primary_hack:
      t = check_exp_prim_gack(env, primary->d.exp);
      break;
    case ae_primary_array:
      t = check_exp_prim_array(env, primary->d.array);
      break;
    case ae_primary_char:
      t = t_int;
      break;
  }
  return primary->self->type = t;
}

ANN Type check_exp_array(const Env env, const Exp_Array* array) { GWDEBUG_EXE
  Type t_base, t;
  m_uint depth = 0;
  Exp e = array->indices->exp_list;

  CHECK_OO((t_base = check_exp(env, array->base)))
  CHECK_OO(check_exp(env, array->indices->exp_list))
  while(e) {
    depth++;
    if(isa(e->type, t_int) < 0)
      CHECK_BO(err_msg(TYPE_,  e->pos, "array index %i must be of type 'int', not '%s'",
            depth, e->type->name))
    e = e->next;
  }
  if(depth != array->indices->depth)
    CHECK_BO(err_msg(TYPE_, array->pos, "invalid array acces expression."))

  while(t_base && array->indices->depth > t_base->array_depth) {
     depth -= t_base->array_depth;
     if(t_base->parent)
       t_base = t_base->parent;
     else
       CHECK_BO(err_msg(TYPE_,  array->pos,
             "array subscripts (%i) exceeds defined dimension (%i)",
             array->indices->depth, t_base->array_depth))
  }
  if(depth == t_base->array_depth)
    t = array_base(t_base);
  else
    t = array_type(array_base(t_base), t_base->array_depth - depth);
  return t;
}

ANN m_bool compat_func(const Func_Def lhs, const Func_Def rhs) {
  Arg_List e1 = lhs->arg_list;
  Arg_List e2 = rhs->arg_list;

  while(e1 && e2) {
    if(e1->type != e2->type)
      return -1;
    e1 = e1->next;
    e2 = e2->next;
  }
  if(e1 || e2)
    return -1;
  return 1;
}

ANN static Type_List mk_type_list(const Env env, const Type type) {
  m_uint i;
  Nspc nspc = type->info;
  struct Vector_ v;
  vector_init(&v);
  if(!type->info)
    vector_add(&v, (vtype)type->name);
  while(nspc && nspc != env->curr && nspc != env->global_nspc) {
    vector_add(&v, (vtype)s_name(insert_symbol((nspc->name))));
    nspc = nspc->parent;
  }
  ID_List id = NULL;
  Type_Decl* td = NULL;
  for(i = vector_size(&v) + 1; --i;)
    id = prepend_id_list(insert_symbol((m_str)vector_at(&v, i - 1)), id, 0);
  vector_release(&v);
  td = new_type_decl(id, 0, 0);
  if(type->array_depth) {
    Array_Sub array = new_array_sub(NULL, 0);
    array->depth = type->array_depth;
    add_type_decl_array(td, array);
  }
  return new_type_list(td, NULL, 0);
}

ANN static m_bool func_match_inner(const Env env, const Exp e, const Type t,
  const m_bool implicit, const m_bool specific) {
  const m_bool match = (specific ? e->type == t : isa(e->type, t) > 0) &&
    e->type->array_depth == t->array_depth &&
    array_base(e->type) == array_base(t);
  if(!match && implicit) {
    const struct Implicit imp = { e, t };
    struct Op_Import opi = { op_implicit, e->type, t, NULL,
      NULL, NULL, (m_uint)&imp };
    return op_check(env, &opi) ? 1 : -1;
  }
  return match ? 1 : -1;
}

__attribute__((nonnull(1,2)))
static Func find_func_match_actual(const Env env, Func func, const Exp args,
  const m_bool implicit, const m_bool specific) {
  do {
    Exp e = args;
    Arg_List e1 = func->def->arg_list;

    while(e) {
      if(!e1) {
        if(GET_FLAG(func->def, ae_flag_variadic))
          return func;
        CHECK_OO(func->next);
        return find_func_match_actual(env, func->next, args, implicit, specific);
      }
      if(func_match_inner(env, e, e1->type, implicit, specific) < 0)
          goto moveon;
      e = e->next;
      e1 = e1->next;
    }
    if(!e1)
      return func;
moveon:;
  } while((func = func->next));
  return NULL;
}

__attribute__((nonnull(1,2)))
static Func find_func_match(const Env env, const Func up, Exp args) {
  Func func;
  if(args && isa(args->type, t_void) > 0)
    args = NULL;
  if((func = find_func_match_actual(env, up, args, 0, 1)) ||
     (func = find_func_match_actual(env, up, args, 1, 1)) ||
     (func = find_func_match_actual(env, up, args, 0, 0)) ||
     (func = find_func_match_actual(env, up, args, 1, 0)))
    return func;
  return NULL;
}

ANN static m_bool find_template_match_inner(const Env env, const Exp_Func* exp, const Func_Def def) {
  const m_bool ret = traverse_func_def(env, def);
  nspc_pop_type(env->curr);
  if(ret < 0 || !check_exp(env, exp->func) ||
     (exp->args  && !check_exp(env, exp->args)))
    return -1;
  return 1;
}

ANN static Value template_get_ready(const Env env, const Value v, const m_str tmpl,
    const m_uint len, const m_uint i) {
  char c[len + 2];
  snprintf(c, len + 2, "%s<%s>@%" INT_F "@%s", v->name, tmpl, i, env->curr->name);
  return v->owner_class ? find_value(v->owner_class, insert_symbol(c)) :
      nspc_lookup_value1(env->curr, insert_symbol(c));
}

ANN static m_bool template_set_env(const Env env, const Value v) {
  vector_add(&env->nspc_stack, (vtype)env->curr);
  env->curr = v->owner;
  vector_add(&env->class_stack, (vtype)env->class_def);
  env->class_def = v->owner_class;
  env->class_scope = 0; // should keep former value somewhere
  return 1;
}

ANN Func find_template_match(const Env env, const Value v, const Exp_Func* exp_func) {
  const Exp func = exp_func->func;
  const Exp args = exp_func->args;
  const Type_List types = exp_func->tmpl ? exp_func->tmpl->types : NULL; // check me
  Func m_func = exp_func->m_func;
  m_uint i;
  m_int mismatch = 0;
  const m_uint digit = num_digit(v->func_num_overloads + 1);
  const m_uint len = strlen(v->name) + strlen(env->curr->name);
  const m_str tmpl_name = tl2str(env, types);
  const m_uint tlen = strlen(tmpl_name);


  CHECK_BO(template_set_env(env, v))
  for(i = 0; i < v->func_num_overloads + 1; i++) {
    Func_Def def = NULL;
    Func_Def base = NULL;
    Value value = template_get_ready(env, v, tmpl_name, len + tlen + digit + 3, i);
    if(value)
      base = def = value->d.func_ref->def;
    else {
      char name[len + digit + 13];
      snprintf(name, len + digit + 13, "%s<template>@%" INT_F "@%s", v->name, i, env->curr->name);
      if(!(value = v->owner_class ? find_value(v->owner_class, insert_symbol(name)) :
            nspc_lookup_value1(env->curr, insert_symbol(name))))
      continue;
      mismatch = 0;
      base = value->d.func_ref->def;
      def = new_func_def(base->flag,
                base->type_decl, func->d.exp_primary.d.var,
                base->arg_list, base->code, func->pos);
      def->tmpl = new_tmpl_list(value->d.func_ref->def->tmpl->list, i);
      UNSET_FLAG(base, ae_flag_template);
      SET_FLAG(def, ae_flag_template);
      if((mismatch = template_match(base->tmpl->list, types)) < 0)
        goto next;
    }
    if(template_push_types(env, base->tmpl->list, types) < 0)
      goto next;
    if(find_template_match_inner(env, exp_func, def) < 0)
      goto next;
    Func next = def->d.func->next;
    def->d.func->next = NULL;
    m_func = find_func_match(env, def->d.func, args);
    def->d.func->next = next;
    if(m_func) {
      free(tmpl_name);
      env_pop_class(env);
      SET_FLAG(base, ae_flag_template);
      SET_FLAG(m_func, ae_flag_checked | ae_flag_template);
      return m_func;
    }
next:
    if(def->d.func) // still leaks
      def->d.func->def = NULL;
    free_func_def(def);
  }
  free(tmpl_name);
  if(mismatch < 0)
    CHECK_BO(err_msg(TYPE_, exp_func->pos, "template type number mismatch."))
  env_pop_class(env);
  (void)err_msg(TYPE_, exp_func->pos, "arguments do not match for template call");
  return NULL;
}

ANN static void print_current_args(Exp e) {
  gw_err("and not");
  gw_err("\n\t");
  do {
    gw_err(" \033[32m%s\033[0m", e->type->name);
    if(e->type->array_depth)
      REM_REF(e->type)
    if(e->next)
      gw_err(",");
  } while((e = e->next));
  gw_err("\n");
}

static void print_arg(Arg_List e) {
  while(e) {
    gw_err(" \033[32m%s\033[0m \033[1m%s\033[0m", e->type->name,
        s_name(e->var_decl->xid));
    e = e->next;
    if(e)
      gw_err(",");
  }
}

__attribute__((nonnull(1)))
static void* function_alternative(const Type f, Exp args){
  if(err_msg(TYPE_, 0, "argument type(s) do not match for function. should be :") < 0){}
  Func up = f->d.func;
  while(up) {
    Arg_List e = up->def->arg_list;
    gw_err("\t");
    if(!e)
      gw_err("\033[32mvoid\033[0m");
    print_arg(e);
    up = up->next;
    gw_err(". (%s)\n%s", f->name, up ? "or :" : "");
  }
  print_current_args(args);
  return NULL;
}

ANN static Value get_template_value(const Env env, const Exp exp_func) {
  Value v = NULL;
  if(exp_func->exp_type == ae_exp_primary)
    v = nspc_lookup_value1(env->curr, exp_func->d.exp_primary.d.var);
  else if(exp_func->exp_type == ae_exp_dot)
    v = find_value(exp_func->d.exp_dot.t_base, exp_func->d.exp_dot.xid);
  if(v)
    UNSET_FLAG(v->d.func_ref->def, ae_flag_template);
  else
    CHECK_BO(err_msg(TYPE_, exp_func->pos,
      "unhandled expression type '%" UINT_F "\' in template call.",
      exp_func->exp_type))
  return v;
}

ANN static m_uint get_type_number(ID_List list) {
  m_uint type_number = 0;
  do type_number++;
  while((list = list->next));
  return type_number;
}

ANN static Func get_template_func(const Env env, const Exp_Func* func, const Value v) {
  const Func f = find_template_match(env, v, func);
  if(f) {
    env->current->tmpl = new_tmpl_call(func->tmpl->types);
    env->current->tmpl->base = v->d.func_ref->def->tmpl->list;
    return f;
  }
  if(err_msg(TYPE_, func->pos, "function is template. automatic type guess not fully implemented yet.\n"
                   "\tplease provide template types. eg: '<type1, type2, ...>'") < 0){}
  return NULL;
}

ANN static Type check_exp_call_template(const Env env, const Exp exp_func,
    const Exp args, Func* m_func) {
  m_uint type_number, args_number = 0;
  Value value;
  ID_List list;
  CHECK_OO((value = get_template_value(env, exp_func)))
  type_number = get_type_number(value->d.func_ref->def->tmpl->list);

  list = value->d.func_ref->def->tmpl->list;
  Type_List tl[type_number];
  while(list) {
    Arg_List arg = value->d.func_ref->def->arg_list;
    Exp template_arg = args;
    while(arg && template_arg) {
      char path[id_list_len(arg->type_decl->xid)];
      type_path(path, arg->type_decl->xid);
      if(!strcmp(s_name(list->xid), path)) {
        tl[args_number] = mk_type_list(env, template_arg->type);
        if(args_number)
          tl[args_number - 1]->next = tl[args_number];
        args_number++;
        break;
      }
      arg = arg->next;
      template_arg = template_arg->next;
    }
    list = list->next;
  }
  if(args_number < type_number)
    CHECK_BO(err_msg(TYPE_, exp_func->pos,
          "not able to guess types for template call."))
  Tmpl_Call tmpl = { tl[0], NULL };
  Exp_Func tmp_func = { exp_func, args, NULL, &tmpl, NULL, 0};
  *m_func = get_template_func(env, &tmp_func, value);
  return *m_func ? (*m_func)->def->ret_type : NULL;
}

__attribute__((nonnull(1)))
static m_bool check_exp_call1_template(const Env env, const Func func) { GWDEBUG_EXE
  Value value;
  if(!func)
    return 1;
  value = func->value_ref;
  if(value->owner_class && GET_FLAG(value->owner_class, ae_flag_template) &&
     !GET_FLAG(value->owner_class, ae_flag_builtin)) {
    Class_Def def = value->owner_class->def;
    CHECK_BB(template_push_types(env, def->tmpl->list.list, def->tmpl->base))
    CHECK_BB(traverse_class_def(env, def))
    nspc_pop_type(env->curr);
  }
  return 1;
}

ANN static m_bool check_exp_call1_check(const Env env, const Exp exp_func, Value* ptr) { GWDEBUG_EXE
  if(!(exp_func->type = check_exp(env, exp_func)))
    CHECK_BB(err_msg(TYPE_, exp_func->pos,
          "function call using a non-existing function"))
  if(isa(exp_func->type, t_function) < 0)
    CHECK_BB(err_msg(TYPE_, exp_func->pos,
          "function call using a non-function value"))
  if(exp_func->exp_type == ae_exp_primary && exp_func->d.exp_primary.value &&
    !GET_FLAG(exp_func->d.exp_primary.value, ae_flag_const))
      *ptr = exp_func->d.exp_primary.value;
  return 1;
}

__attribute__((nonnull(1,2)))
Type check_exp_call1(const Env env, const Exp exp_func, const Exp args, Func *m_func) { GWDEBUG_EXE
  Func func = NULL;
  Value ptr = NULL;
  CHECK_BO(check_exp_call1_check(env, exp_func, &ptr))
  CHECK_BO(check_exp_call1_template(env, exp_func->type->d.func))
  if(args)
    CHECK_OO(check_exp(env, args))
  if(!exp_func->type->d.func)
    return check_exp_call_template(env, exp_func, args, m_func);
  if(!(func = find_func_match(env, exp_func->type->d.func, args)))
    return function_alternative(exp_func->type, args);
  if(ptr) {
    Func f = new_func_simple();
    memcpy(f, func, sizeof(struct Func_));
    f->value_ref = ptr;
    SET_FLAG(ptr, ae_flag_func); // there might be a better place
    if(ptr->d.func_ref)
      f->next = ptr->d.func_ref;
    func = ptr->d.func_ref = f;
  }
  *m_func = func;
  return func->def->ret_type;
}

ANN Type opck_fptr_at(const Env env, Exp_Binary* bin ) {
  Type r_nspc, l_nspc = NULL;
  m_uint i;
  Func f1 = NULL;
  Func f2 = NULL;
  Value v = NULL;
  Type ret_type;

  bin->rhs->emit_var = 1;
  if(bin->rhs->exp_type == ae_exp_primary) {
    v = nspc_lookup_value1(env->curr, bin->rhs->d.exp_primary.d.var);
    f1 = v->d.func_ref ? v->d.func_ref :
      nspc_lookup_func1(env->curr, insert_symbol(v->m_type->name));
  } else if(bin->rhs->exp_type == ae_exp_dot) {
    Type t = bin->rhs->d.exp_dot.t_base;
    if(isa(t, t_class) > 0)
      t = t->d.base_type;
    v = find_value(t, bin->rhs->d.exp_dot.xid);
    f1 = find_func(t, insert_symbol(v->m_type->name));
  } else if(bin->rhs->exp_type == ae_exp_decl) {
    v = bin->rhs->d.exp_decl.list->self->value;
    f1 = v->m_type->d.func;
  } else
    CHECK_BO(err_msg(TYPE_, bin->pos, "unhandled function pointer assignement (rhs)."))
    r_nspc = (v->owner_class && GET_FLAG(v, ae_flag_member)) ? v->owner_class : NULL;
  if(bin->lhs->exp_type == ae_exp_primary) {
    v = nspc_lookup_value1(env->curr, bin->lhs->d.exp_primary.d.var);
    f2 = nspc_lookup_func1(env->curr, insert_symbol(v->m_type->name));
    l_nspc = (v->owner_class && GET_FLAG(v, ae_flag_member)) ? v->owner_class : NULL;
  } else if(bin->lhs->exp_type == ae_exp_dot) {
    Type t = bin->lhs->d.exp_dot.t_base;
    if(isa(t, t_class) > 0)
      t = t->d.base_type;
    v = find_value(t, bin->lhs->d.exp_dot.xid);
    f2 = v->d.func_ref;
    l_nspc = (v->owner_class && GET_FLAG(v, ae_flag_member)) ? v->owner_class : NULL;
  } else
    CHECK_BO(err_msg(TYPE_, bin->pos, "unhandled function pointer assignement (lhs)."))
    if((r_nspc && l_nspc) && (r_nspc != l_nspc))
      CHECK_BO(err_msg(TYPE_, bin->pos, "can't assign member function to member function pointer of an other class"))
    if(!r_nspc && l_nspc)
      CHECK_BO(err_msg(TYPE_, bin->pos, "can't assign member function to non member function pointer"))
    if(r_nspc && !l_nspc)
      CHECK_BO(err_msg(TYPE_, bin->pos, "can't assign non member function to member function pointer"))
    if(!f1 || !f2)
      CHECK_BO(err_msg(TYPE_, bin->pos, "function not found."))
    if(isa(f1->def->ret_type, f2->def->ret_type) < 0)
      CHECK_BO(err_msg(TYPE_, 0, "return type '%s' does not match '%s'\n\t... in pointer assignement",
           f1->def->ret_type->name, f2->def->ret_type->name))
    for(i = 0; i <= v->func_num_overloads; i++) {
      if(bin->lhs->exp_type == ae_exp_primary) {
        m_str c = f2 && f2->def ? s_name(f2->def->name) : NULL;
        char name[(c ? strlen(c) : 0) + strlen(env->curr->name) + num_digit(v->func_num_overloads) + 3];
        sprintf(name, "%s@%" INT_F "@%s", c, i, env->curr->name);
        f2 = nspc_lookup_func1(env->curr, insert_symbol(name));
      }
      if(f2 && compat_func(f1->def, f2->def) > 0) {
        bin->func = f2;
        ret_type = f1->value_ref->m_type;
        return ret_type;
      }
    }
  CHECK_BO(err_msg(TYPE_, 0, "no match found for function '%s'", f2 ? s_name(f2->def->name) : "[broken]"))
  return NULL;
}

ANN static m_bool multi_decl(const Exp e, const Operator op) {
  if(e->exp_type == ae_exp_decl &&  e->d.exp_decl.list->next)
    CHECK_BB(err_msg(TYPE_, e->pos,
          "cant '%s' from/to a multi-variable declaration.", op2str(op)))
  return 1;
}

ANN static Type check_exp_binary(const Env env, const Exp_Binary* bin) { GWDEBUG_EXE
  struct Op_Import opi = { bin->op, NULL, NULL, NULL,
    NULL, NULL, (uintptr_t)bin };

  CHECK_BO(multi_decl(bin->lhs, bin->op));
  CHECK_BO(multi_decl(bin->rhs, bin->op));
  CHECK_OO((opi.lhs = check_exp(env, bin->lhs)))
  CHECK_OO((opi.rhs = check_exp(env, bin->rhs)))
  OP_RET(bin, "binary")
}

ANN static Type check_exp_cast(const Env env, const Exp_Cast* cast) { GWDEBUG_EXE
  const Type t = check_exp(env, cast->exp);
  if(!t) return NULL;
  if(!(cast->self->type = type_decl_resolve(env, cast->type)))
    CHECK_BO(type_unknown(cast->type->xid, "cast expression"))
  struct Op_Import opi = { op_dollar, t, cast->self->type, NULL,
    NULL, NULL, (uintptr_t)cast };
  OP_RET(cast, "cast")
}

ANN static Type check_exp_post(const Env env, const Exp_Postfix* post) { GWDEBUG_EXE
  struct Op_Import opi = { post->op, check_exp(env, post->exp), NULL, NULL,
    NULL, NULL, (uintptr_t)post };
  CHECK_OO(opi.lhs)
  OP_RET(post, "postfix");
}

ANN static Type check_exp_dur(const Env env, const Exp_Dur* dur) { GWDEBUG_EXE
  const Type base = check_exp(env, dur->base);
  const Type unit = check_exp(env, dur->unit);
  if(!base || !unit)
    return NULL;
  if(isa(base, t_int) < 0 && isa(base, t_float) < 0) {
    CHECK_BO(err_msg(TYPE_, dur->base->pos,
          "invalid type '%s' in prefix of dur expression...\n"
          "    (must be of type 'int' or 'float')", base->name))
  }
  if(isa(unit, t_dur) < 0) {
    CHECK_BO(err_msg(TYPE_, dur->unit->pos,
          "invalid type '%s' in postfix of dur expression...\n"
          "    (must be of type 'dur')", unit->name))
  }
  return unit;
}

ANN static Type check_exp_call(const Env env, Exp_Func* call) { GWDEBUG_EXE
  if(call->tmpl) {
    Func ret;
    Value v = NULL;
    if(call->func->exp_type == ae_exp_primary)
      v = nspc_lookup_value1(env->curr, call->func->d.exp_primary.d.var);
    else if(call->func->exp_type == ae_exp_dot) {
      Type t;
      CHECK_OO(check_exp(env, call->func)) // → puts this up ?
      t = call->func->d.exp_dot.t_base;
      if(isa(t, t_class) > 0)
        t = t->d.base_type;
      v = find_value(t, call->func->d.exp_dot.xid);
      } else
      CHECK_BO(err_msg(TYPE_, call->pos, "invalid template call."))
    if(!v)
      CHECK_BO(err_msg(TYPE_, call->pos,
            " template call of non-existant function."))
    if(!GET_FLAG(v, ae_flag_func))
      CHECK_BO(err_msg(TYPE_, call->pos,
            "template call of non-function value."))
    if(!v->d.func_ref->def->tmpl)
      CHECK_BO(err_msg(TYPE_, call->pos,
            "template call of non-template function."))
    CHECK_OO((ret = find_template_match(env, v, call)))
      call->m_func = ret;
    return ret->def->ret_type;
  }
  env->current = call;
  return check_exp_call1(env, call->func, call->args, &call->m_func);
}

//static
// Move me ?
ANN Type check_exp_unary_spork(const Env env, const Stmt code) { GWDEBUG_EXE
  CHECK_BO(check_stmt(env, code))
  if(env->func) {
    env->class_scope++;
    nspc_push_value(env->curr);
    m_bool ret = check_stmt(env, code);
    nspc_pop_value(env->curr);
    env->class_scope--;
    return (ret > 0) ? t_shred : NULL;
  } else if(check_stmt(env, code) < 0) { GWDEBUG_EXE
    CHECK_BO(err_msg(TYPE_, code->pos, "problem in evaluating sporked code"))
  }
  return t_shred;
}

ANN static Type check_exp_unary(const Env env, const Exp_Unary* unary) { GWDEBUG_EXE
  Type t = NULL;
  struct Op_Import opi = { unary->op, NULL, NULL, NULL,
    NULL, NULL, (uintptr_t)unary };
  if(unary->exp) {
    CHECK_OO((t = check_exp(env, unary->exp)))
    opi.rhs = unary->exp->type;
  }
  OP_RET(unary, "unary")
}

ANN static Type check_exp_if(const Env env, const Exp_If* exp_if) { GWDEBUG_EXE
  const Type cond     = check_exp(env, exp_if->cond);
  const Type if_exp   = check_exp(env, exp_if->if_exp);
  const Type else_exp = check_exp(env, exp_if->else_exp);
  Type ret;
  if(!cond || !if_exp || !else_exp)
    return NULL;

  if(isa(cond, t_int) < 0 && isa(cond, t_float) < 0)
    CHECK_BO(err_msg(TYPE_, exp_if->pos,
          "Invalid type '%s' in if expression condition.", cond->name))
    if(!(ret = find_common_anc(if_exp, else_exp)))
      CHECK_BO(err_msg(TYPE_, exp_if->pos,
            "incompatible types '%s' and '%s' in if expression...",
            if_exp->name, else_exp->name))
      return ret;
}

ANN static m_bool check_nspc(const Exp_Dot* member, const Type t) { GWDEBUG_EXE
  if(!t->info)
    CHECK_BB(err_msg(TYPE_,  member->base->pos,
          "type '%s' does not have members - invalid use in dot expression of %s",
          t->name, s_name(member->xid)))
  return 1;
}

ANN static m_bool check_enum(const Exp exp, const Value v) { GWDEBUG_EXE
  if(GET_FLAG(v, ae_flag_enum))
    exp->meta = ae_meta_value;
  return 1;
}

ANN static Value get_dot_value(const Exp_Dot* member, const Type the_base) {
  const Value value = find_value(the_base, member->xid);

  if(!value) {
    m_uint i, len = strlen(the_base->name) + the_base->array_depth * 2 + 1;
    char s[len];
    memset(s, 0, len);
    strcpy(s, the_base->name);
    for(i = 0; i < the_base->array_depth; i++)
      strcat(s, "[]");
    CHECK_BO(err_msg(TYPE_,  member->base->pos,
          "class '%s' has no member '%s'", s, s_name(member->xid)))
  }
  return value;
}

ANN static Type check_exp_dot(const Env env, Exp_Dot* member) { GWDEBUG_EXE
  Value value;
  Type  the_base;
  m_bool base_static;
  const m_str str = s_name(member->xid);

  CHECK_OO((member->t_base = check_exp(env, member->base)))
  base_static = isa(member->t_base, t_class) > 0;
  the_base = base_static ? member->t_base->d.base_type : member->t_base;
  CHECK_BO(check_nspc(member, the_base))
  if(!strcmp(str, "this") && base_static)
    CHECK_BO(err_msg(TYPE_,  member->pos,
          "keyword 'this' must be associated with object instance..."))
  CHECK_OO((value = get_dot_value(member, the_base)))
  if(GET_FLAG(value, ae_flag_private) &&
        (!env->class_def || isa(env->class_def, value->owner_class) < 0))
    CHECK_BO(err_msg(TYPE_,  member->pos,
          "can't access private '%s' outside of class...", value->name))
  if(base_static && GET_FLAG(value, ae_flag_member))
    CHECK_BO(err_msg(TYPE_, member->pos,
          "cannot access member '%s.%s' without object instance...",
          the_base->name, str))
  CHECK_BO(check_enum(member->self, value))
  return value->m_type;
}

ANN m_bool check_stmt_fptr(const Env env, const Stmt_Ptr ptr) { GWDEBUG_EXE
  const Type t     = nspc_lookup_type1(env->curr, ptr->xid);
  t->size    = SZ_INT;
  t->name    = s_name(ptr->xid);
  t->parent  = t_func_ptr;
  nspc_add_type(env->curr, ptr->xid, t);
  ptr->m_type = t;
  t->d.func = ptr->func;
  return 1;
}

ANN static m_bool check_stmt_type(const Env env, const Stmt_Typedef stmt) { GWDEBUG_EXE
  return stmt->m_type->def ? check_class_def(env, stmt->m_type->def) : 1;
}

ANN Type check_exp(const Env env, const Exp exp) { GWDEBUG_EXE
  Exp curr = exp;
  while(curr) {
    curr->type = NULL;
    switch(curr->exp_type) {
      case ae_exp_primary:
        curr->type = check_exp_primary(env, &curr->d.exp_primary);
        break;
      case ae_exp_decl:
        curr->type = check_exp_decl(env, &curr->d.exp_decl);
        break;
      case ae_exp_unary:
        curr->type = check_exp_unary(env, &curr->d.exp_unary);
        break;
      case ae_exp_binary:
        curr->type = check_exp_binary(env, &curr->d.exp_binary);
        break;
      case ae_exp_post:
        curr->type = check_exp_post(env, &curr->d.exp_post);
        break;
      case ae_exp_dur:
        curr->type = check_exp_dur(env, &curr->d.exp_dur);
        break;
      case ae_exp_cast:
        curr->type = check_exp_cast(env, &curr->d.exp_cast);
        break;
      case ae_exp_call:
        curr->type = check_exp_call(env, &curr->d.exp_func);
        break;
      case ae_exp_if:
        curr->type = check_exp_if(env, &curr->d.exp_if);
        break;
      case ae_exp_dot:
        curr->type = check_exp_dot(env, &curr->d.exp_dot);
        break;
      case ae_exp_array:
        curr->type = check_exp_array(env, &curr->d.exp_array);
        break;
    }
    CHECK_OO(curr->type)
    curr = curr->next;
  }
  return exp->type;
}

ANN m_bool check_stmt_enum(const Env env, const Stmt_Enum stmt) { GWDEBUG_EXE
  ID_List list = stmt->list;
  while(list) {
    const Value v = nspc_lookup_value0(env->curr, list->xid);
    if(env->class_def) {
      SET_FLAG(v, ae_flag_static);
      v->offset = env->class_def->info->class_data_size;
      env->class_def->info->class_data_size += SZ_INT;
    }
    list = list->next;
  }
  return 1;

}

ANN static m_bool check_stmt_code(const Env env, const Stmt_Code stmt, const m_bool push) { GWDEBUG_EXE
  m_bool ret;
  if(!stmt->stmt_list)
    return 1;
  env->class_scope++;
  if(push)
    nspc_push_value(env->curr);
  ret = check_stmt_list(env, stmt->stmt_list);
  if(push)
    nspc_pop_value(env->curr);
  env->class_scope--;
  return ret;
}

ANN static m_bool check_flow(const Exp exp) { GWDEBUG_EXE
  if(isa(exp->type, t_int) > 0 || isa(exp->type, t_float) > 0 ||
     isa(exp->type, t_dur) > 0 || isa(exp->type, t_time)  > 0)
    return 1;
  CHECK_BB(err_msg(TYPE_,  exp->pos,
        "invalid type '%s'", exp->type->name))
  return 1;
}

ANN static m_bool check_stmt_while(const Env env, const Stmt_While stmt) { GWDEBUG_EXE
  CHECK_OB(check_exp(env, stmt->cond))
  if(check_flow(stmt->cond) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->cond->pos, "\t... in 'while' condition."))
  vector_add(&env->breaks, (vtype)stmt->self);
  vector_add(&env->conts, (vtype)stmt->self);
  CHECK_BB(check_stmt(env, stmt->body))
  vector_pop(&env->breaks);
  vector_pop(&env->conts);
  return 1;
}

ANN static m_bool check_stmt_until(const Env env, const Stmt_Until stmt) { GWDEBUG_EXE
  CHECK_OB(check_exp(env, stmt->cond))
  if(check_flow(stmt->cond) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->cond->pos, "\t... in 'until' condition."))
  vector_add(&env->breaks, (vtype)stmt->self);
  CHECK_BB(check_stmt(env, stmt->body))
  vector_pop(&env->breaks);
  return 1;
}

ANN static m_bool check_breaks(const Env env, const Stmt a, const Stmt b) { GWDEBUG_EXE
  vector_add(&env->breaks, (vtype)a);
  CHECK_BB(check_stmt(env, b))
  vector_pop(&env->breaks);
  return 1;
}

ANN static m_bool check_stmt_for(const Env env, const Stmt_For stmt) { GWDEBUG_EXE
  CHECK_BB(check_stmt(env, stmt->c1))
  CHECK_BB(check_stmt(env, stmt->c2))
  if(!stmt->c2 || !stmt->c2->d.stmt_exp.val) {
    CHECK_BB(err_msg(EMIT_, stmt->pos, "empty for loop condition...",
                     "...(note: explicitly use 'true' if it's the intent)",
                     "...(e.g., 'for(; true;){ /*...*/ }')"))
  }
  if(check_flow(stmt->c2->d.stmt_exp.val) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->c2->pos, "\t... in 'for' condition."))
  if(stmt->c3)
    CHECK_OB(check_exp(env, stmt->c3))
  return check_breaks(env, stmt->self, stmt->body);
}

ANN static m_bool check_stmt_auto(const Env env, const Stmt_Auto stmt) { GWDEBUG_EXE
  Type t = check_exp(env, stmt->exp);
  Type ptr = array_base(t);
  const m_uint depth = t->array_depth - 1;
  if(GET_FLAG(t, ae_flag_typedef))
    t = t->parent;
  if(!t || !ptr || isa(t, t_array) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->pos, "type '%s' is not array.\n"
          " This is not allowed in auto loop", stmt->exp->type->name))
  if(stmt->is_ptr) {
    struct ID_List_   id0, id;
    struct Type_List_ tl;
    struct Array_Sub_ array;
    Type_Decl td0, td;
    memset(&id0, 0, sizeof(struct ID_List_));
    memset(&id, 0, sizeof(struct ID_List_));
    memset(&tl, 0, sizeof(struct Type_List_));
    memset(&array, 0, sizeof(struct Array_Sub_));
    memset(&td0, 0, sizeof(Type_Decl));
    memset(&td, 0, sizeof(Type_Decl));
    id.xid  = insert_symbol("Ptr");
    id0.xid = insert_symbol(ptr->name);
    td0.xid = &id0;
    td.xid = &id;
    tl.list = &td0;
    td.types = &tl;
    if(depth) {
      array.depth = depth;
      td.array = &array;
    }
    ptr = type_decl_resolve(env, &td);
    if(!GET_FLAG(ptr, ae_flag_checked))
      check_class_def(env, ptr->def);
  } else
  t = depth ? array_type(ptr, depth) : ptr;
  stmt->v = new_value(t, s_name(stmt->sym));
  SET_FLAG(stmt->v, ae_flag_checked);
  nspc_add_value(env->curr, stmt->sym, stmt->v);
  return check_breaks(env, stmt->self, stmt->body);
}

ANN static m_bool check_stmt_loop(const Env env, const Stmt_Loop stmt) { GWDEBUG_EXE
  const Type type = check_exp(env, stmt->cond);

  CHECK_OB(type)
  if(isa(type, t_float) > 0)
    stmt->cond->cast_to = t_int;
  else if(isa(type, t_int) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->pos,
          "loop * conditional must be of type 'int'..."))
  return check_breaks(env, stmt->self, stmt->body);
}

ANN static m_bool check_stmt_if(const Env env, const Stmt_If stmt) { GWDEBUG_EXE
  CHECK_OB(check_exp(env, stmt->cond))
  if(isa(stmt->cond->type, t_object) > 0)
    if(check_flow(stmt->cond) < 0)
      CHECK_BB(err_msg(TYPE_, stmt->cond->pos, "\t... in 'if' condition."))
  CHECK_BB(check_stmt(env, stmt->if_body))
  if(stmt->else_body)
    CHECK_BB(check_stmt(env, stmt->else_body))
  return 1;
}

ANN static m_bool check_stmt_return(const Env env, const Stmt_Return stmt) { GWDEBUG_EXE
  Type ret_type = NULL;
  if(!env->func)
    CHECK_BB(err_msg(TYPE_, stmt->pos,
          "'return' statement found outside function definition"))
    if(stmt->val)
      CHECK_OB((ret_type = check_exp(env, stmt->val)))
    else
      ret_type = t_void;
  if(isa(ret_type, t_null) > 0 &&
     isa(env->func->def->ret_type, t_object) > 0)
    return 1;
  if(isa(ret_type, env->func->def->ret_type) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->pos,
          "invalid return type '%s' -- expecting '%s'",
          ret_type->name, env->func->def->ret_type->name))
  return 1;
}

ANN static m_bool check_stmt_continue(const Env env, const Stmt_Continue cont) { GWDEBUG_EXE
  if(!vector_size(&env->breaks))
    CHECK_BB(err_msg(TYPE_,  cont->pos,
             "'continue' found outside of for/while/until..."))
    return 1;
}

ANN static m_bool check_stmt_break(const Env env, const Stmt_Break cont) { GWDEBUG_EXE
  if(!vector_size(&env->breaks))
    CHECK_BB(err_msg(TYPE_,  cont->pos,
             "'break' found outside of for/while/until..."))
    return 1;
}

ANN static m_bool check_stmt_switch(const Env env, const Stmt_Switch a) { GWDEBUG_EXE
  const Type t = check_exp(env, a->val);
  if(!t || isa(t, t_int) < 0)
    CHECK_BB(err_msg(TYPE_, a->pos,
          "invalid type '%s' in switch expression. should be 'int'",
          t ? t->name : "unknown"))
  return check_breaks(env, a->self, a->stmt);
}

ANN static m_bool check_stmt_case(const Env env, const Stmt_Case stmt) { GWDEBUG_EXE
  if(stmt->val->exp_type  != ae_exp_primary &&
      stmt->val->exp_type != ae_exp_dot)
    CHECK_BB(err_msg(TYPE_, stmt->pos,
          "unhandled expression type '%i'", stmt->val->exp_type))
  const Type t = check_exp(env, stmt->val);
  if(!t || isa(t, t_int) < 0)
    CHECK_BB(err_msg(TYPE_, stmt->pos,
          "invalid type '%s' case expression. should be 'int'",
          t ? t->name : "unknown"))
  return 1;
}

ANN static m_bool check_stmt_gotolabel(const Env env, const Stmt_Goto_Label stmt) { GWDEBUG_EXE
  Map m, label = env_label(env);
  const m_uint* key = env->class_def && !env->func ?
                (m_uint*)env->class_def : (m_uint*)env->func;
  Stmt_Goto_Label ref;
  if(stmt->is_label)
    return 1;
  if(!(m = label->ptr ? (Map)map_get(label, (vtype)key) : NULL))
    CHECK_BB(err_msg(TYPE_, stmt->pos,
                     "label '%s' used but not defined", s_name(stmt->name)))
    if(!(ref = (Stmt_Goto_Label)map_get(m, (vtype)stmt->name))) {
      m_uint i;
      for(i = 0; i < map_size(m); i++) {
        ref = (Stmt_Goto_Label)map_at(m, i);
        vector_release(&ref->data.v);
      }
      CHECK_BB(err_msg(TYPE_, stmt->pos,
              "label '%s' used but not defined", s_name(stmt->name)))
    }
  vector_add(&ref->data.v, (vtype)stmt);
  return 1;
}

ANN m_bool check_stmt_union(const Env env, const Stmt_Union stmt) { GWDEBUG_EXE
  Decl_List l = stmt->l;
  if(stmt->xid) {
    if(env->class_def) {
      if(GET_FLAG(stmt, ae_flag_static))
        check_exp_decl_static(env, stmt->value, stmt->pos);
      else
        check_exp_decl_member(env->curr, stmt->value);
    }
    env_push_class(env, stmt->value->m_type);
  } else if(env->class_def)  {
      if(GET_FLAG(stmt, ae_flag_static)) {
        env->class_def->info->class_data_size += SZ_INT;
        env->class_def->info->offset += SZ_INT;
      }
      else
        stmt->o = env->class_def->info->offset;
  }
  while(l) {
    CHECK_OB(check_exp(env, l->self))
    if(isa(l->self->type, t_object) > 0 && !GET_FLAG(l->self->d.exp_decl.type, ae_flag_ref))
      CHECK_BB(err_msg(TYPE_, l->self->pos, "In union, Objects must be declared as reference (use '@')"))
    if(l->self->type->size > stmt->s)
      stmt->s = l->self->type->size;
    l = l->next;
  }
  if(stmt->xid)
    env_pop_class(env);
  return 1;
}

ANN static m_bool check_stmt(const Env env, const Stmt stmt) { GWDEBUG_EXE
  m_bool ret = 1;
  switch(stmt->stmt_type) {
    case ae_stmt_exp:
      if(stmt->d.stmt_exp.val)
        ret = (check_exp(env, stmt->d.stmt_exp.val) ? 1 : -1);
      break;
    case ae_stmt_code:
      SCOPE(ret = check_stmt_code(env, &stmt->d.stmt_code, 1))
      break;
    case ae_stmt_return:
      ret = check_stmt_return(env, &stmt->d.stmt_return);
      break;
    case ae_stmt_break:
      ret = check_stmt_break(env, &stmt->d.stmt_break);
      break;
    case ae_stmt_continue:
      ret = check_stmt_continue(env, &stmt->d.stmt_continue);
      break;
    case ae_stmt_if:
      NSPC(ret = check_stmt_if(env, &stmt->d.stmt_if))
      break;
    case ae_stmt_while:
      NSPC(ret = check_stmt_while(env, &stmt->d.stmt_while))
      break;
    case ae_stmt_until:
      NSPC(ret = check_stmt_until(env, &stmt->d.stmt_until))
      break;
    case ae_stmt_for:
      NSPC(ret = check_stmt_for(env, &stmt->d.stmt_for))
      break;
    case ae_stmt_auto:
      NSPC(ret = check_stmt_auto(env, &stmt->d.stmt_auto))
      break;
    case ae_stmt_loop:
      NSPC(ret = check_stmt_loop(env, &stmt->d.stmt_loop))
      break;
    case ae_stmt_switch:
      NSPC(ret = check_stmt_switch(env, &stmt->d.stmt_switch))
      break;
    case ae_stmt_case:
      ret = check_stmt_case(env, &stmt->d.stmt_case);
      break;
    case ae_stmt_enum:
      ret = check_stmt_enum(env, &stmt->d.stmt_enum);
      break;
    case ae_stmt_gotolabel:
      ret = check_stmt_gotolabel(env, &stmt->d.stmt_gotolabel);
      break;
    case ae_stmt_funcptr:
      ret = check_stmt_fptr(env, &stmt->d.stmt_ptr);
      break;
    case ae_stmt_typedef:
      ret = check_stmt_type(env, &stmt->d.stmt_type);
      break;
    case ae_stmt_union:
      ret = check_stmt_union(env, &stmt->d.stmt_union);
      break;
  }
  return ret;
}

ANN static m_bool check_stmt_list(const Env env, Stmt_List l) { GWDEBUG_EXE
  do CHECK_BB(check_stmt(env, l->stmt))
  while((l = l->next));
  return 1;
}

ANN static m_bool check_signature_match(const Func_Def f, const Func parent) { GWDEBUG_EXE
  const m_str c_name  = f->d.func->value_ref->owner_class->name;
  const m_str p_name = parent->value_ref->owner_class->name;
  const m_str f_name = s_name(f->name);
  if(GET_FLAG(parent->def, ae_flag_static) || GET_FLAG(f, ae_flag_static)) {
    CHECK_BB(err_msg(TYPE_, f->pos,
          "function '%s.%s' resembles '%s.%s' but cannot override...\n"
          "\t...(reason: '%s.%s' is declared as 'static')",
          c_name, f_name, p_name, c_name,
          GET_FLAG(f, ae_flag_static) ? c_name : p_name, f_name))
  }
  if(isa(f->ret_type, parent->def->ret_type) <  0) {
    CHECK_BB(err_msg(TYPE_, f->pos,
          "function signatures differ in return type...\n"
          "\tfunction '%s.%s' matches '%s.%s' but cannot override...",
          c_name, f_name, p_name, f_name))
  }
  return 1;
}

ANN static m_bool parent_match_actual(const Env env, const Func_Def f, m_bool* parent_match) {
  Value v;
  const Func func = f->d.func;
  if((v = find_value(env->class_def->parent, f->name))) {
    Func parent_func = v->d.func_ref;
    while(parent_func && !*parent_match) {
      if(compat_func(f, parent_func->def) < 0) {
        parent_func = parent_func->next;
        continue;
      }
      CHECK_BB(check_signature_match(f, parent_func))
      *parent_match = 1;
      func->vt_index = parent_func->vt_index;
      vector_set(&env->curr->vtable, func->vt_index, (vtype)func);
      func->value_ref->name = func->name = parent_func->name;
    }
  }
  return 1;
}

ANN static m_bool check_parent_match(const Env env, const Func_Def f) { GWDEBUG_EXE
  m_bool parent_match = 0;
  Type parent = env->class_def->parent;
  const Func func = f->d.func;
  while(parent && !parent_match) {
    if(find_value(env->class_def->parent, f->name))
      CHECK_BB(parent_match_actual(env, f, &parent_match))
    parent = parent->parent;
  }
  if(GET_FLAG(func, ae_flag_member) && !parent_match) {
    if(!env->curr->vtable.ptr)
      vector_init(&env->curr->vtable);
    func->vt_index = vector_size(&env->curr->vtable);
    vector_add(&env->curr->vtable, (vtype)func);
  }
  return 1;
}

ANN static m_bool check_func_args(const Env env, Arg_List arg_list) { GWDEBUG_EXE
  m_uint count = 1;
  do {
    const Value v = arg_list->var_decl->value;
    if(nspc_lookup_value0(env->curr, arg_list->var_decl->xid))
      CHECK_BB(err_msg(TYPE_, arg_list->pos,
                    "argument %i '%s' is already defined in this scope\n",
                    count, s_name(arg_list->var_decl->xid)))
    SET_FLAG(v, ae_flag_checked);
    nspc_add_value(env->curr, arg_list->var_decl->xid, v);
    count++;
  } while((arg_list = arg_list->next));
  return 1;
}

ANN static m_bool check_func_overload_inner(const Env env, const Func_Def def,
  const m_str name, const m_uint j) {
  sprintf(name, "%s@%" INT_F "@%s", s_name(def->name), j, env->curr->name);
  const Func f2 = nspc_lookup_func2(env->curr, insert_symbol(name));
  if(compat_func(def, f2->def) > 0) {
    CHECK_BB(err_msg(TYPE_, f2->def->pos,
        "global function '%s' already defined for those arguments",
        s_name(def->name)))
  }
  return 1;
}

ANN static m_bool check_func_overload(const Env env, const Func_Def f) { GWDEBUG_EXE
  m_uint i, j;
  const Value v = f->d.func->value_ref;
  if(!f->tmpl || !f->tmpl->base) {
    char name[strlen(s_name(f->name)) + strlen(env->curr->name) +
                                      num_digit(v->func_num_overloads) + 3];
    for(i = 0; i <= v->func_num_overloads; i++) {
      sprintf(name, "%s@%" INT_F "@%s", s_name(f->name), i, env->curr->name);
      Func f1 = nspc_lookup_func2(env->curr, insert_symbol(name));
      for(j = 1; j <= v->func_num_overloads; j++) {
        if(i != j)
          CHECK_BB(check_func_overload_inner(env, f1->def, name, j))
      }
    }
  }
  return 1;
}

ANN static m_bool check_func_def_override(const Env env, const Func_Def f) { GWDEBUG_EXE
  Value override = NULL;
  Func func = f->d.func;
  if(env->class_def)
    override = find_value(env->class_def->parent, f->name);
  else if(func->value_ref->func_num_overloads && (!f->tmpl || !f->tmpl->base))
    CHECK_BB(check_func_overload(env, f))
  if(env->class_def &&  override && isa(override->m_type, t_function) < 0)
    CHECK_BB(err_msg(TYPE_, f->pos,
                     "function name '%s' conflicts with previously defined value...\n"
                     "\tfrom super class '%s'...",
                     s_name(f->name), override->owner_class->name))
  if(override) {
    while(func->next)
      func = func->next;
    func->next = override->d.func_ref;
  }
  return 1;
}

ANN static Value set_variadic(const Env env) {
  const Value variadic = new_value(t_vararg, "vararg");
  SET_FLAG(variadic, ae_flag_checked);
  nspc_add_value(env->curr, insert_symbol("vararg"), variadic);
  return variadic;
}

ANN m_bool check_func_def(const Env env, const Func_Def f) { GWDEBUG_EXE
  const Func func = get_func(env, f);
  Value variadic = NULL;
  m_bool ret = 1;

  if(tmpl_list_base(f->tmpl))
    return 1;
  if(!GET_FLAG(f, ae_flag_builtin))
    CHECK_BB(check_func_def_override(env, f))
  if(env->class_def)
    CHECK_BB(check_parent_match(env, f))
  env->func = func;
  nspc_push_value(env->curr);
  if(f->arg_list)
    ret = check_func_args(env, f->arg_list);
  if(GET_FLAG(f, ae_flag_variadic))
    variadic = set_variadic(env);
  if(f->code && check_stmt_code(env, &f->code->d.stmt_code, 0) < 0)
    ret = err_msg(TYPE_, f->type_decl->pos,
                  "...in function '%s'", s_name(f->name));
  if(GET_FLAG(f, ae_flag_variadic))
    REM_REF(variadic)
  if(GET_FLAG(f, ae_flag_builtin))
    func->code->stack_depth = f->stack_depth;
  nspc_pop_value(env->curr);
  env->func = NULL;
  return ret;
}

ANN static m_bool check_section(const Env env, const Section* section) { GWDEBUG_EXE
  const ae_Section_Type t = section->section_type;
  if(t == ae_section_stmt)
    CHECK_BB(check_stmt_list(env, section->d.stmt_list))
  else if(t == ae_section_func)
    CHECK_BB(check_func_def(env, section->d.func_def))
  else if(t == ae_section_class)
    CHECK_BB(check_class_def(env, section->d.class_def))
  return 1;
}

ANN static m_bool check_class_parent(const Env env, const Class_Def class_def) { GWDEBUG_EXE
  if(class_def->ext->array) {
    CHECK_OB(check_exp(env, class_def->ext->array->exp_list))
    CHECK_BB(check_exp_array_subscripts(class_def->ext->array->exp_list))
  }
  if(class_def->ext->types) {
    const Type t = class_def->type->parent->array_depth ?
      array_base(class_def->type->parent) : class_def->type->parent;
    if(class_def->tmpl)
      CHECK_BB(template_push_types(env, class_def->tmpl->list.list, class_def->tmpl->base))
    CHECK_BB(template_push_types(env, t->def->tmpl->list.list, class_def->ext->types))
    CHECK_BB(traverse_class_def(env, t->def))
    nspc_pop_type(env->curr);
    if(class_def->tmpl)
      nspc_pop_type(env->curr);
  }
  if(isa(class_def->type->parent, t_object) < 0)
    CHECK_BB(err_msg(TYPE_, class_def->ext->pos,
            "cannot extend primitive type '%s'", class_def->type->parent->name))
  if(!GET_FLAG(class_def->type->parent, ae_flag_checked)) {
    if(GET_FLAG(class_def->ext, ae_flag_typedef)) // ??????
      CHECK_BB(check_class_def(env, class_def->type->parent->def))
    else
      CHECK_BB(err_msg(TYPE_, class_def->ext->pos,
            "cannot extend incomplete type '%s'\n"
            "\t...(note: the parent's declaration must preceed child's)",
            class_def->type->parent->name))
  }
  if(GET_FLAG(class_def->type->parent, ae_flag_typedef))
    SET_FLAG(class_def->type, ae_flag_typedef);
  return 1;
}

ANN m_bool check_class_def(const Env env, const Class_Def class_def) { GWDEBUG_EXE
  Class_Body body = class_def->body;
  const Type the_class = class_def->type;

  if(tmpl_class_base(class_def->tmpl))
    return 1;
  if(class_def->ext)
    CHECK_BB(check_class_parent(env, class_def))
  else
    the_class->parent = t_object;
  the_class->info->offset = the_class->parent->info->offset;
  if(the_class->parent->info->vtable.ptr)
    vector_copy2(&the_class->parent->info->vtable, &the_class->info->vtable);

  CHECK_BB(env_push_class(env, the_class))
  while(body) {
    CHECK_BB(check_section(env, body->section))
    body = body->next;
  }
  CHECK_BB(env_pop_class(env))
  SET_FLAG(the_class, ae_flag_checked);
  SET_FLAG(class_def->type, ae_flag_check);
  return 1;
}

ANN m_bool check_ast(const Env env, Ast ast) { GWDEBUG_EXE
  do CHECK_BB(check_section(env, ast->section))
  while((ast = ast->next));
  return 1;
}
