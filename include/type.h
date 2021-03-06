#ifndef __TYPE
#define __TYPE

#include "nspc.h"
#include "env.h"

struct Type_ {
  m_str     name;
  m_uint    size;
  Type      parent;
  m_uint    xid;
  Nspc      info;
  Nspc      owner;
  m_uint    array_depth;
  Class_Def def;
  union type_data {
    Func      func;
    Type      base_type;
  } d;
  m_uint flag;
  struct VM_Object_ obj;
};

ANN m_bool type_engine_check_prog(const Env, const Ast, const m_str);
__attribute__((nonnull(2)))
ANEW Type new_type(const m_uint xid, const m_str name, const Type);
ANEW ANN Type type_copy(const Type type) ANN;
Env type_engine_init(VM*, const Vector) __attribute__((malloc)) ANN;
Value find_value(const Type, const Symbol) ANN;
Func find_func(const Type, const Symbol) ANN;
Type find_type(const Env, ID_List) ANN;
m_bool isa(const Type, const Type) ANN;
m_bool isres(const Symbol) ANN;
Type array_type(const Type, const m_uint) ANN;
m_bool check_array_empty(const Array_Sub, const m_str) ANN;
Type find_common_anc(const Type, const Type) ANN;
m_uint id_list_len(ID_List) ANN;
void type_path(m_str, const ID_List) ANN;
__attribute__((nonnull(1,2)))
m_bool env_add_value(const Env env, const m_str, const Type, const m_bool, void* value);
ANN m_bool env_add_type(const Env, const Type);
m_int str2char(const m_str, const m_int) ANN;
__attribute__((pure))
m_uint num_digit(const m_uint);
Type array_base(Type) ANN;
m_bool type_ref(Type) ANN;
m_bool prim_ref(const Type_Decl*, const Type) ANN;


Type t_int, t_float, t_dur, t_time, t_now, t_complex, t_polar;
Type t_void;
Type t_vec3, t_vec4;
Type t_func_ptr;
Type t_function;
Type t_class;
Type t_null;
Type t_object;
Type t_shred;
Type t_event;
Type t_ugen;
Type t_vararg;
Type t_string;
Type t_gack;
Type t_ptr;
Type t_array;
Type t_union;
#endif

