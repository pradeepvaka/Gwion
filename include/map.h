#ifndef __MAP
#define __MAP
#include "defs.h"
#include <symbol.h>

typedef m_uint vtype;
#include "map_private.h"
typedef struct Vector_ * Vector;
typedef struct Map_    * Map;

ANEW extern       Vector new_vector();
ANN extern       void   vector_init(Vector);
ANN extern Vector vector_copy(Vector);
ANN extern       void   vector_copy2(const __restrict__ Vector, __restrict__ Vector);
ANN extern m_int  vector_find(const Vector, const vtype);

ANN static inline void vector_set(const Vector v, const vtype i, const vtype arg) {
  VPTR(v, i) = arg;
}
ANN static inline vtype vector_front(const Vector v) {
  return VLEN(v) ? VPTR(v, 0) : 0;
}
ANN static inline vtype vector_at(Vector v, const vtype i) {
  return (i >= VLEN(v)) ? 0 : VPTR(v, i);
}
ANN static inline vtype vector_back(const Vector v) {
  return VPTR(v, VLEN(v) - 1);
}
ANN static inline vtype vector_size(const Vector v) {
  return VLEN(v);
}

extern ANN       void  vector_add(const Vector, const vtype);
extern ANN       void  vector_rem(const Vector, const vtype);
extern ANN       vtype vector_pop(const Vector);
extern ANN       void  vector_clear(const Vector);
extern ANN       void  free_vector(Vector vector);
extern ANN       void  vector_release(Vector vector);

ANEW extern Map new_map();
extern     void map_init();
extern ANN vtype map_get(const Map, const vtype);
extern ANN vtype map_at(const Map, const vtype);
extern ANN void map_set(const Map, const vtype, const vtype);
extern ANN void map_remove(const Map, const vtype);
extern ANN void map_commit(const __restrict__ Map, __restrict__ const Map);
extern ANN void map_clear(const Map);
extern ANN void free_map(Map);
extern ANN void map_release(Map);
ANN static inline vtype map_size(const Map map) {
  return VLEN(map);
}

//extern ANEW          Scope  new_scope();
extern ANN       void   scope_init(Scope);
extern ANEW ANN Vector scope_get(const Scope);
extern ANN vtype  scope_lookup0(const Scope, const Symbol);
extern ANN vtype  scope_lookup1(const Scope, const Symbol);
extern ANN vtype  scope_lookup2(const Scope, const Symbol);
extern ANN       void   scope_add(const Scope, const Symbol, const vtype);
extern ANN       void   scope_commit(const Scope);
extern ANN       void   scope_push(const Scope);
extern ANN       void   scope_pop(const Scope);
//extern ANN       void   free_scope(Scope);
extern ANN       void   scope_release(Scope);
#endif
