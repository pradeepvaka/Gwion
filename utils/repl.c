#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <pthread.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "vm.h"
#include "type.h"
#include "err_msg.h"
#include "instr.h"
#include "context.h"
#include "compile.h"
#include "traverse.h"
#include "shreduler_private.h"

#define PROMPT_SZ 128

static m_bool accept, chctx, fork, add, sys;

static inline int _bind_cr(int count, int key) {
  if(accept) {
    printf("\n");
    return rl_done = 1;
  }
  printf("\n(...)");
  return 0;
}

static inline int _bind_accept(int count, int key) {
  accept = 1;
  return 0;
}

static inline int _bind_add(int count, int key) {
  accept = rl_done = 1;
  add = key == 97 ? 1 : -1;
  return 0;
}


static inline int _bind_ctx(int count, int key) {
  printf("\n");
  chctx = 1;
  return rl_done = 1;
}

static inline int _bind_fork(int count, int key) {
  fork = 1;
  return 0;
}

static inline int _bind_sys(int count, int key) {
  printf("\n");
  sys = 1;
  return accept = rl_done = 1;
}

static inline VM_Shred repl_shred() {
  VM_Code code = new_vm_code(NULL, 0, 0, "repl");
  VM_Shred shred = new_vm_shred(code);
  return shred;
}

ANN static void eval(VM* vm, VM_Shred shred, const m_str line) {
  if(shred == vm->shreduler->list) {
    gw_err("shred[%"UINT_F"] is running.please use '\\C-f' to spork it\n", shred->xid);
    return;
  }
  FILE* f = fmemopen(line, strlen(line), "r");
  Ast ast = parse("repl", f);
  m_str str;
  if(!ast)
    goto close;
  str = strdup("repl");
  if(traverse_ast(vm->emit->env, ast) < 0)
    goto close;
  if(emit_ast(vm->emit, ast, str) < 0)
    goto close;
  ((Instr)vector_back(&vm->emit->code->code))->execute = EOC2;
  vm->emit->code->name = str;
  if(shred->code)
    free_vm_code(shred->code);
  shred->code = emit_code(vm->emit);
  free_ast(ast);
  vm_add_shred(vm, shred);
close:
  fclose(f);
}

struct Repl {
  Context ctx;
  VM_Shred shred;
};

ANN static struct Repl* new_repl(const m_str name) {
  struct Repl* repl = xmalloc(sizeof(struct Repl));
  repl->shred = repl_shred();
  repl->ctx = new_context(NULL, name);
  return repl;
}

ANN static void free_repl(struct Repl* repl, VM* vm) {
  if(repl->shred->code->instr) {
    ((Instr)vector_back(repl->shred->code->instr))->execute = EOC;
    repl->shred->next_pc = vector_size(repl->shred->code->instr) - 2;
    vm_add_shred(vm, repl->shred);
  } else
    shreduler_remove(vm->shreduler, repl->shred, 1);
  REM_REF(repl->ctx);
  free(repl);
}

ANN static struct Repl* repl_ctx(struct Repl* repl, Vector v, VM* vm) {
  struct Repl* r = NULL;
  accept = 1;
  m_str ln = readline("\033[1mcontext:\033[0m ");
  for(m_uint i = vector_size(v) + 1; --i;) {
    struct Repl* s = (struct Repl*)vector_at(v, i-1);
    if(!strcmp(ln, s->ctx->filename)) {
       r = s;
       break;
    }
  }
  if(!r) {
      printf("creating new context \033[1m'%s'\033[0m\n", ln);
      r = new_repl(ln);
      vector_add(v, (vtype)r);
  }
  if(repl != r) {
    unload_context(repl->ctx, vm->emit->env);
    repl = r;
    load_context(repl->ctx, vm->emit->env);
  }
  chctx = 0;
  return repl;
}

ANN static void repl_fork(struct Repl* repl) {
  printf("fork shred [%"UINT_F"]\n", repl->shred->xid);
  VM_Shred old = repl->shred;
  repl->shred = repl_shred();
  old->parent = repl->shred;
  if(!repl->shred->child.ptr)
    vector_init(&repl->shred->child);
  vector_add(&repl->shred->child, (vtype)old);
  memcpy(repl->shred->_mem, old->_mem, SIZEOF_MEM);
  memcpy(repl->shred->_reg, old->_reg, SIZEOF_REG);
  fork = 0;
}

static void repl_sys() {
  pid_t cpid = fork;
  int status;
  m_str cmd = readline("command: ");
  system(cmd);
  free(cmd);
  waitpid(cpid, &status, 0);
  sys = 0;
}

ANN static void repl_add(VM* vm) {
  m_str line = readline(add > 0 ? "add file: " : "rem file:");
  if(add > 0)
    compile(vm, line);
  else {
    m_uint index = strtol(line, NULL, 10);
    vm_remove(vm, index);
  }
  free(line);
  add = 0;
}

ANN static m_str repl_prompt(struct Repl* repl) {
  char prompt[PROMPT_SZ];
  if(repl->shred->xid)
    snprintf(prompt, PROMPT_SZ, "'%s'\033[30;3;1m[%"UINT_F"]\033[32m=>\033[0m ", repl->ctx->filename, repl->shred->xid);
  else
    snprintf(prompt, PROMPT_SZ, "'%s'\033[30;3;1m[!]\033[32m=>\033[0m ", repl->ctx->filename);
  accept = 0;
  return readline(prompt);
}

ANN static void repl_cmd(struct Repl* repl, VM* vm, Vector v) {
    if(chctx)
      repl = repl_ctx(repl, v, vm);
    if(sys)
      repl_sys();
    if(add)
      repl_add(vm);
    if(fork)
      repl_fork(repl);
}

ANN static struct Repl* repl_ini(VM* vm, Vector v) {
  struct Repl* repl = new_repl("repl");
  vector_init(v);
  vector_add(v, (vtype)repl);
  load_context(repl->ctx, vm->emit->env);
  read_history(NULL);
  rl_bind_key('\n', _bind_cr);\
  rl_bind_key('\r', _bind_cr);\
  rl_bind_keyseq("\\M-g", _bind_accept);\
  rl_bind_keyseq("\\M-f", _bind_fork);\
  rl_bind_keyseq("\\M-c", _bind_ctx);\
  rl_bind_keyseq("\\M-a", _bind_add);\
  rl_bind_keyseq("\\M-r", _bind_add);\
  rl_bind_keyseq("\\M-s", _bind_sys);\
  return repl;
}

ANN static void repl_end(struct Repl* repl, VM* vm, Vector v) {
  unload_context(repl->ctx, vm->emit->env);
  for(m_uint i = vector_size(v) + 1; --i;)
    free_repl((struct Repl*)vector_at(v, i-1), vm);
  vector_release(v);
  write_history(NULL);
  vm->is_running = 0;
}

ANN static void* repl_process(void* data) {
  VM* vm = (VM*)data;
  struct Vector_ v;
  struct Repl* repl = repl_ini(vm, &v);
  while(vm->is_running) {
    char* line = repl_prompt(repl);
    if(!line)
      break;
    if(strlen(line)) {
      eval(vm, repl->shred, line);
      add_history(line);
    }
    repl_cmd(repl, vm, &v);
    free(line);
  }
  repl_end(repl, vm, &v);
  return NULL;
}

ANN void repl_init(VM* vm, pthread_t* p) {
  pthread_create(p, NULL, repl_process, vm);
#ifndef __linux__
  pthread_detach(*p);
#endif
}
