#include <stdio.h>
#include "mpc.h"

static char input[2048];

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

enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUNC, LVAL_SEXPR, LVAL_QEXPR };

#define LERR_DIV_ZERO "Division by zero!"
#define LERR_BAD_OP "Invalid operation"
#define LERR_BAD_NUM "Invalid number"
#define LERR_UNDEFINED_SYMBOL "Undefined Symbol"
#define LERR_NOT_VALID_SEXPR "Invalid sexpr, first element is not a function"
#define LERR_TOO_MANY_ARGS(funcname) "Function " funcname " has too many args"
#define LERR_TOO_FEW_ARGS(funcname) "Function " funcname " has too few args"
#define LERR_INCORRECT_ARGS_TYPES(funcname) "Function " funcname " passed incorrect types"
#define LERR_CANNOT_DEFINE_NON_SYM "Invalid name used to define symbol"
#define LERR_DEF_SYM_VAL_MISMATCH "Mismatch between number of symbols and values provided"
#define LERR_LAMBDA_ARGS_COUNT "Lambda definition must have parameter and body definition"

lval* eval_sexpr(env* e, lval* v);
lval* eval(env* e, lval* v);
lval* read(mpc_ast_t* tree);
lval* call(env* e, lval* f, lval* args);

void add_builtin(char* name, lbuiltin func);
void add_all_builtins();
lval* builtin_op(env* e, lval* args, char* op);
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

lval* lval_num(long n);
lval* lval_err(char* code);
lval* lval_sym(char* symbol);
lval* lval_sexpr(void);
lval* lval_qexpr(void);
lval* lval_func(lbuiltin func);
lval* lval_lambda(env* parent, lval* params, lval* body);
lval* lval_take(lval* v, int i);
lval* lval_pop(lval* v, int i);
void lval_add(lval* sexpr, lval* addition);
void lval_del(lval* v);
lval* lval_copy(lval* v);

void lval_print(lval* v);
void lval_println(lval* v);
void lval_print_expr(lval* v, char open, char close);

env* env_create(env* parent);
env* env_copy(env* e);
void env_delete(env* e);
void env_put(env* e, char* key, lval* val);
lval* env_get(env* e, lval* key);

// The global environment for the program
env* e = NULL;

int main(int argc, char** argv) {

  e = env_create(NULL);
  if (e == NULL) {
    fputs("ERROR: Failed to create environment, quitting...", stdout);
    return -1;
  }

  add_all_builtins();

  puts("Welcome to this (so far) untitled Lisp");
  puts("Press Ctrl+c to exit\n");

  mpc_parser_t* number = mpc_new("number");
  mpc_parser_t* symbol = mpc_new("symbol");
  mpc_parser_t* qexpr = mpc_new("qexpr");
  mpc_parser_t* sexpr = mpc_new("sexpr");
  mpc_parser_t* expr = mpc_new("expr");
  mpc_parser_t* code = mpc_new("code");

  mpca_lang(MPCA_LANG_DEFAULT,
    "number: /-?[0-9]+/; \
    symbol: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/; \
    qexpr: '[' <expr>* ']'; \
    sexpr: '(' <expr>* ')'; \
    expr: <number> | <symbol> | <sexpr> | <qexpr>; \
    code: /^/ <expr>* /$/;",
    number, symbol, sexpr, qexpr, expr, code);

  while (1) {
    fputs("lisp> ", stdout);
    fflush(stdout);

    fgets(input, 2048, stdin);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, code, &r)) {
      // TODO: These print statements can be hidden behind debug flags
      // mpc_ast_print(r.output);
      lval* expr = read(r.output);
      // lval_print_expr(expr, '(', ')');
      // putchar('\n');
      // fflush(stdout);
      
      expr = eval(e, expr);
      lval_println(expr);

      lval_del(expr);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

  env_delete(e);
  mpc_cleanup(6, number, symbol, sexpr, qexpr, expr, code);

  return 0;
}

lval* read(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    errno = 0;
    long num = strtol(tree->contents, NULL, 10);
    return (errno == ERANGE) ? lval_err(LERR_BAD_NUM) : lval_num(num);
  }

  if (strstr(tree->tag, "symbol")) {
    return lval_sym(tree->contents);
  }

  lval* expressions;

  if ((strcmp(tree->tag, ">") == 0) || strstr(tree->tag, "sexpr")) {
    expressions = lval_sexpr();
  }
  if (strstr(tree->tag, "qexpr")) {
    expressions = lval_qexpr();
  }

  for (int i = 0; i < tree->children_num; i++) {
    if ((strcmp(tree->children[i]->tag, "char") == 0)
        || (strcmp(tree->children[i]->tag, "regex") == 0)) {
      continue;
    }
    lval_add(expressions, read(tree->children[i]));
  }

  return expressions;
}

lval* eval(env* e, lval* v) {
  if (v->type == LVAL_SYM) {
    lval* r = env_get(e, v);
    lval_del(v);
    return r;
  }
  if (v->type == LVAL_SEXPR) {
    return eval_sexpr(e, v);
  }
  return v;
}

lval* eval_sexpr(env* e, lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->exprs[i] = eval(e, v->exprs[i]);
  }

  for (int i = 0; i < v->count; i++) {
    if (v->exprs[i]->type == LVAL_ERR) {
      return lval_take(v, i);
    }
  }

  if (v->count == 0) {
    return v;
  }

  if (v->count == 1) {
    return lval_take(v, 0);
  }

  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUNC) {
    lval_del(f);
    lval_del(v);
    return lval_err(LERR_NOT_VALID_SEXPR);
  }

  lval* result = call(e, f, v);
  lval_del(f);
  return result;
}

lval* call(env* e, lval* f, lval* args) {
  if (f->builtin != NULL) {
    return f->builtin(e, args);
  }

  while (args->count) {
    if (f->params->count == 0) {
      lval_del(args);
      return lval_err(LERR_TOO_MANY_ARGS("function call"));
    }

    lval* symbol = lval_pop(f->params, 0);
    lval* value = lval_pop(args, 0);

    env_put(f->scope, symbol->symbol, value);

    lval_del(symbol);
    lval_del(value);
  }

  lval_del(args);

  if (f->params->count == 0) {
    lval* b = lval_sexpr();
    lval_add(b, lval_copy(f->body));
    return builtin_eval(f->scope, b);
  } else {
    return lval_copy(f);
  }
}

void add_builtin(char* name, lbuiltin func) {
  lval* f = lval_func(func);
  env_put(e, name, f);
}

void add_all_builtins() {
  add_builtin("array", builtin_array);
  add_builtin("head", builtin_head);
  add_builtin("tail", builtin_tail);
  add_builtin("concat", builtin_concat);
  add_builtin("eval", builtin_eval);
  add_builtin("def", builtin_def);
  add_builtin("\\", builtin_lambda);

  add_builtin("+", builtin_add);
  add_builtin("-", builtin_sub);
  add_builtin("*", builtin_mul);
  add_builtin("/", builtin_div);
}

lval* builtin_op(env* e, lval* args, char* op) {
  for (int i = 0; i < args->count; i++) {
    if (args->exprs[i]->type != LVAL_NUM) {
      lval_del(args);
      return lval_err("Cannot operate on non-number");
    }
  }

  lval* x = lval_pop(args, 0);

  if ((strcmp(op, "-") == 0) && (args->count == 0)) {
    x->num = - x->num;
  }

  while (args->count > 0) {
    lval* y = lval_pop(args, 0);

    if (strcmp(op, "+") == 0) {
      x->num += y->num;
    }
    if (strcmp(op, "-") == 0) {
      x->num -= y->num;
    }
    if (strcmp(op, "*") == 0) {
      x->num *= y->num;
    }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
	lval_del(x);
	lval_del(y);
	x = lval_err(LERR_DIV_ZERO);
	break;
      }
      x->num /= y->num;
    }

    lval_del(y);
  }

  lval_del(args);
  return x;
}

lval* builtin_add(env* e, lval* args) {
  return builtin_op(e, args, "+");
}

lval* builtin_sub(env* e, lval* args) {
  return builtin_op(e, args, "-");
}

lval* builtin_mul(env* e, lval* args) {
  return builtin_op(e, args, "*");
}

lval* builtin_div(env* e, lval* args) {
  return builtin_op(e, args, "/");
}

/* Given a qexpr will return the head (aka first) expression */
lval* builtin_head(env*e, lval* args) {
  if (args->count == 0) {
    lval_del(args);
    return lval_err(LERR_TOO_FEW_ARGS("head"));
  }

  if (args->count != 1) {
    lval_del(args);
    return lval_err(LERR_TOO_MANY_ARGS("head"));
  }

  if (args->exprs[0]->type != LVAL_QEXPR) {
    lval_del(args);
    return lval_err(LERR_INCORRECT_ARGS_TYPES("head"));
  }

  lval* result = lval_take(args->exprs[0], 0);
  lval_del(args);
  return result;
}

/* Given a qexpr will return the tail (aka last) expression */
lval* builtin_tail(env* e, lval* args) {
  if (args->count == 0) {
    lval_del(args);
    return lval_err(LERR_TOO_FEW_ARGS("tail"));
  }

  if (args->count != 1) {
    lval_del(args);
    return lval_err(LERR_TOO_MANY_ARGS("tail"));
  }

  if (args->exprs[0]->type != LVAL_QEXPR) {
    lval_del(args);
    return lval_err(LERR_INCORRECT_ARGS_TYPES("tail"));
  }

  lval* result = lval_take(args->exprs[0], args->exprs[0]->count - 1);
  lval_del(args);
  return result;
}

/* Converts a sexpr into a qexpr */
lval* builtin_array(env* e, lval* args) {
  args->type = LVAL_QEXPR;
  return args;
}

/* Will convert a qexpr into a sexpr and eval it */
lval* builtin_eval(env* e, lval* args) {
  if (args->count == 0) {
    lval_del(args);
    return lval_err(LERR_TOO_FEW_ARGS("eval"));
  }

  // TODO: This should be removed once multiple expressions on the same line are allowed
  if (args->count != 1) {
    lval_del(args);
    return lval_err(LERR_TOO_MANY_ARGS("eval"));
  }

  lval* x = lval_pop(args, 0);
  lval_del(args);
  x->type = LVAL_SEXPR;
  return eval(e, x);
}

/* Given a sexpr with multiple qexprs as its children, will combine the qexprs to a single one */
lval* builtin_concat(env* e, lval* args) {
  for (int i = 0; i < args->count; i++) {
    if (args->exprs[i]->type != LVAL_QEXPR) {
      lval_del(args);
      return lval_err(LERR_INCORRECT_ARGS_TYPES("tail"));
    }    
  }

  lval* accumulate = lval_pop(args, 0);

  while (args->count) {
    while (args->exprs[0]->count) {
      lval_add(accumulate, lval_pop(args->exprs[0], 0));
    }
    lval_del(lval_pop(args, 0));
  }

  lval_del(args);
  return accumulate;
}

lval* builtin_def(env* e, lval* args) {
  if (args->exprs[0]->type != LVAL_QEXPR) {
    lval_del(args);
    return lval_err(LERR_INCORRECT_ARGS_TYPES("def"));
  }

  // first arg is a qexpr of the names of the symbols
  // the remaining args are the values to be mapped onto the symbols
  lval* names = args->exprs[0];

  for (int i = 0; i < names->count; i++) {
    if (names->exprs[i]->type != LVAL_SYM) {
      lval_del(args);
      return lval_err(LERR_CANNOT_DEFINE_NON_SYM);
    }
  }

  if (names->count != (args->count - 1)) {
    lval_del(args);
    return lval_err(LERR_DEF_SYM_VAL_MISMATCH);
  }

  for (int i = 0; i < names->count; i++) {
    env_put(e, names->exprs[i]->symbol, args->exprs[i + 1]);
  }

  lval_del(args);
  return lval_sexpr();
}

lval* builtin_lambda(env* e, lval* args) {
  if (args->count != 2) {
    lval_del(args);
    return lval_err(LERR_LAMBDA_ARGS_COUNT);
  }

  if (args->exprs[0]->type != LVAL_QEXPR) {
    lval_del(args);
    return lval_err(LERR_INCORRECT_ARGS_TYPES("lambda"));
  }

  if (args->exprs[1]->type != LVAL_QEXPR) {
    lval_del(args);
    return lval_err(LERR_INCORRECT_ARGS_TYPES("lambda"));
  }

  for (int i = 0; i < args->exprs[0]->count; i++) {
    if (args->exprs[0]->exprs[i]->type != LVAL_SYM) {
      lval_del(args);
      return lval_err(LERR_CANNOT_DEFINE_NON_SYM);
    }
  }

  lval* params = lval_pop(args, 0);
  lval* body = lval_pop(args, 0);
  lval_del(args);

  return lval_lambda(e, params, body);
}

lval* lval_num(long n) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = n;
  return v;
}

lval* lval_err(char* msg) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->error = malloc(sizeof(strlen(msg) + 1));
  strcpy(v->error, msg);
  return v;
}

lval* lval_sym(char* symbol) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->symbol = malloc(sizeof(strlen(symbol) + 1));
  strcpy(v->symbol, symbol);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->exprs = NULL;
  return v;
}

lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->exprs = NULL;
  return v;
}

lval* lval_func(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;
  v->builtin = func;
  return v;
}

lval* lval_lambda(env* parent, lval* params, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;
  v->builtin = NULL;
  
  v->scope = env_create(parent);

  v->params = params;
  v->body = body;
  return v;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

/* Given an expr will take out the element at index i of the subexpressions */
lval* lval_pop(lval* v, int i) {
  lval* x = v->exprs[i];

  memmove(&v->exprs[i], &v->exprs[i + 1], sizeof(lval*) * (v->count - i - 1));

  v->count--;

  // This frees the last memory location that we no longer need when we did the memmove
  v->exprs = realloc(v->exprs, sizeof(lval*) * v->count);
  return x;
}

void lval_add(lval* sexpr, lval* addition) {
  sexpr->count++;
  sexpr->exprs = realloc(sexpr->exprs, sizeof(lval*) * sexpr->count);
  sexpr->exprs[sexpr->count - 1] = addition;
}

void lval_del(lval* v) {
  switch(v->type) {
  case LVAL_NUM:
    break;

  case LVAL_ERR:
    free(v->error);
    break;

  case LVAL_SYM:
    free(v->symbol);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    for (int i = 0; i < v->count; i++) {
      lval_del(v->exprs[i]);
    }
    free(v->exprs);
    break;

  case LVAL_FUNC:
    if (!v->builtin) {
      lval_del(v->params);
      lval_del(v->body);
      env_delete(v->scope);
    }
    break;
  }

  free(v);
}

lval* lval_copy(lval* v) {
  lval* copy = malloc(sizeof(lval));
  copy->type = v->type;

  switch(v->type) {
  case LVAL_NUM:
    copy->num = v->num;
    break;

  case LVAL_ERR:
    copy->error = malloc(strlen(v->error) + 1);
    strcpy(copy->error, v->error);
    break;

  case LVAL_SYM:
    copy->symbol = malloc(strlen(v->symbol) + 1);
    strcpy(copy->symbol, v->symbol);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    copy->count = v->count;
    copy->exprs = malloc(sizeof(lval*) * v->count);
    for (int i = 0; i < v->count; i++) {
      copy->exprs[i] = lval_copy(v->exprs[i]);
    }
    break;

  case LVAL_FUNC:
    if (v->builtin != NULL) {
      copy->builtin = v->builtin;
    } else {
      copy->builtin = NULL;
      copy->params = lval_copy(v->params);
      copy->body = lval_copy(v->body);
      copy->scope = env_copy(v->scope);
    }
    break;
  }

  return copy;
}

void lval_print(lval* v) {
  switch(v->type) {
  case LVAL_NUM:
    printf("%li", v->num);
    break;

  case LVAL_ERR:
    printf("Error: %s", v->error);
    break;

  case LVAL_SYM:
    printf("%s", v->symbol);
    break;

  case LVAL_SEXPR:
    lval_print_expr(v, '(', ')');
    break;

  case LVAL_QEXPR:
    lval_print_expr(v, '[', ']');
    break;

  case LVAL_FUNC:
    if (v->builtin) {
      printf("<function>");
    } else {
      printf("(\\ ");
      lval_print(v->params);
      putchar(' ');
      lval_print(v->body);
      putchar(')');
    }
    break;
  }
}

void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

void lval_print_expr(lval* v, char open, char close) {
  putchar(open);

  for (int i = 0; i < v->count; i++) {
    lval_print(v->exprs[i]);

    if (i != (v->count - 1)) {
      putchar(' ');
    }
  }

  putchar(close);
}

env* env_create(env* parent) {
  env* e = malloc(sizeof(env));
  e->parent = parent;
  e->size = 0;
  e->labels = NULL;
  e->values = NULL;
  return e;
}

env* env_copy(env* e) {
  env* copy = malloc(sizeof(env));
  copy->size = e->size;

  copy->parent = e->parent;
  copy->labels = malloc(sizeof(char**) * copy->size);
  copy->values = malloc(sizeof(lval**) * copy->size);

  for (int i = 0; i < copy->size; i++) {
    copy->labels[i] = malloc(strlen(e->labels[i]) + 1);
    strcpy(copy->labels[i], e->labels[i]);
    copy->values[i] = lval_copy(e->values[i]);
  }

  return copy;
}

void env_delete(env* e) {
  for (int i = 0; i < e->size; i++) {
    free(e->labels[i]);
    lval_del(e->values[i]);
  }
  
  free(e->labels);
  free(e->values);

  free(e);
}

void env_put(env* e, char* key, lval* val) {
  for (int i = 0; i < e->size; i++) {
    if (strcmp(key, e->labels[i]) == 0) {
      lval_del(e->values[i]);
      e->values[i] = lval_copy(val);
      return;
    }
  }

  e->size++;

  e->labels = realloc(e->labels, sizeof(char*) * e->size);
  e->values = realloc(e->values, sizeof(lval*) * e->size);

  e->labels[e->size - 1] = malloc(strlen(key) + 1);
  strcpy(e->labels[e->size - 1], key);
  e->values[e->size - 1] = lval_copy(val);
}

lval* env_get(env* e, lval* key) {

  for (int i = 0; i < e->size; i++) {
    if (strcmp(e->labels[i], key->symbol) == 0) {
      return lval_copy(e->values[i]);
    }
  }

  if (e->parent) {
    return env_get(e->parent, key);
  } else {
    return lval_err(LERR_UNDEFINED_SYMBOL);    
  }
}
