#include "mpc.h"

typedef struct lval lval;
typedef struct env env;

typedef struct env {
  env* parent;
  int size;
  char** labels;
  lval** values;
} env;

typedef lval*(*lbuiltin)(env*, lval*);

typedef struct lval {
  int type;

  union {
    long num;

    char* error;

    char* symbol;

    char* str;

    struct {
      lbuiltin builtin;
      env* scope;
      lval* params;
      lval* body;
    };

    struct {
      int count;
      lval** exprs;
    };
  };
} lval;

enum { LVAL_ERR, LVAL_NUM, LVAL_STR, LVAL_SYM, LVAL_FUNC, LVAL_SEXPR, LVAL_QEXPR };

#define T_ERROR_FUNC_UNEXPECTED_ARGS_NUM "Function %s expected %d args but got %d"
#define T_ERROR_FUNC_INCORRECT_ARG_TYPE "Function %s argument num %d expected %s but got %s"
#define T_ERROR_UNDEFINED_SYMBOL "Undefined Symbol %s"

#define ERROR_DIV_BY_ZERO "Division by zero"
#define ERROR_READ_BAD_NUM "Invalid number"
#define ERROR_EVAL_INVALID_SEXPR "Invalid sexpr, first element is not a function"

lval* eval_sexpr(env* e, lval* sexpr);
lval* eval(env* e, lval* expr);
lval* read(mpc_ast_t* tree);
lval* call(env* e, lval* function, lval* args);

void add_builtin(char* identifier, lbuiltin func);
void add_all_builtins();
lval* builtin_op(env* e, lval* args, char* operator);
lval* builtin_add(env* e, lval* args);
lval* builtin_sub(env* e, lval* args);
lval* builtin_mul(env* e, lval* args);
lval* builtin_div(env* e, lval* args);
lval* builtin_head(env* e, lval* args);
lval* builtin_tail(env* e, lval* args);
lval* builtin_array(env* e, lval* args);
lval* builtin_eval(env* e, lval* args);
lval* builtin_concat(env* e, lval* args);
lval* builtin_def(env* e, lval* args);
lval* builtin_lambda(env* e, lval* args);
lval* builtin_load(env* e, lval* args);
lval* builtin_print(env* e, lval* args);
lval* builtin_error(env* e, lval* args);
lval* builtin_not(env* e, lval* args);
lval* builtin_cmp(env* e, lval* args, char* op);
lval* builtin_gt(env* e, lval* args);
lval* builtin_gte(env* e, lval* args);
lval* builtin_lt(env* e, lval* args);
lval* builtin_lte(env* e, lval* args);
lval* builtin_eq(env* e, lval* args);
lval* builtin_if(env* e, lval* args);

int is_truthy(env* e, lval* val);

lval* lval_num(long num);
lval* lval_str(char* str);
lval* lval_err(char* msgFormat, ...);
lval* lval_sym(char* identifier);
lval* lval_sexpr(void);
lval* lval_qexpr(void);
lval* lval_func(lbuiltin func);
lval* lval_lambda(env* parentEnv, lval* params, lval* body);
lval* lval_take(lval* parentExpr, int index);
lval* lval_pop(lval* v, int i);
void lval_add(lval* sexpr, lval* addition);
void lval_del(lval* v);
lval* lval_copy(lval* v);
int lval_eq(lval* a, lval* b);

void lval_print(lval* v);
void lval_println(lval* v);
void lval_print_expr(lval* v, char open, char close);
char* lval_typename(int typeEnum);

env* env_create(env* parent);
env* env_copy(env* e);
void env_delete(env* e);
void env_put(env* e, char* key, lval* val);
lval* env_get(env* e, lval* key);

#define ASSERT_TRUE_OR_RETURN(condition, args, msg_format, ...)		\
  if (!(condition)) {							\
    lval* err = lval_err(msg_format, ##__VA_ARGS__);			\
    lval_del(args);							\
    return err;								\
  }
