#include <stdio.h>
#include "mpc.h"

static char input[2048];

typedef struct lval lval;
typedef struct lval {
  int type;

  union {
    long num;

    char* error;

    char* symbol;

    struct {
      int count;
      lval** exprs;
    };
  };
} lval;

enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };
#define LERR_DIV_ZERO "Division by zero!"
#define LERR_BAD_OP "Invalid operation"
#define LERR_BAD_NUM "Invalid number"
#define LERR_TOO_MANY_ARGS(funcname) strcat(strcat("Function ", funcname), " has too many args")
#define LERR_TOO_FEW_ARGS(funcname) strcat(strcat("Function ", funcname), " has too few args")
#define LERR_INCORRECT_ARGS_TYPES(funcname) strcat(strcat("Function ", funcname), " passed incorrect types")

lval* eval_sexpr(lval* v);
lval* eval(lval* v);
lval* read(mpc_ast_t* tree);
lval* builtin(lval* args, char* func);
lval* builtin_op(lval* args, char* op);
lval* builtin_head(lval* args);
lval* builtin_tail(lval* args);
lval* builtin_array(lval* args);
lval* builtin_eval(lval* args);
lval* builtin_concat(lval* args);

lval* lval_num(long n);
lval* lval_err(char* code);
lval* lval_sym(char* symbol);
lval* lval_sexpr(void);
lval* lval_qexpr(void);
lval* lval_take(lval* v, int i);
lval* lval_pop(lval* v, int i);
void lval_add(lval* sexpr, lval* addition);
void lval_del(lval* v);

void lval_print(lval* v);
void lval_println(lval* v);
void lval_print_expr(lval* v, char open, char close);


int main(int argc, char** argv) {

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
    symbol: \"array\" | \"head\" | \"tail\" | \"concat\" | \"eval\" \
            | '+' | '-' | '*' | '/'; \
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
      lval_print_expr(expr, '(', ')');
      putchar('\n');
      fflush(stdout);
      
      expr = eval(expr);
      lval_println(expr);

      lval_del(expr);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

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

lval* eval(lval* v) {
  if (v->type == LVAL_SEXPR) {
    return eval_sexpr(v);
  }
  return v;
}

lval* eval_sexpr(lval* v) {
  for (int i = 0; i < v->count; i++) {
    v->exprs[i] = eval(v->exprs[i]);
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
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-Expression doesn't start with symbol.");
  }

  lval* result = builtin(v, f->symbol);
  lval_del(f);
  return result;
}

lval* builtin(lval* args, char* func) {
  if (strcmp("array", func) == 0) {
    return builtin_array(args);
  }
  if (strcmp("head", func) == 0) {
    return builtin_head(args);
  }
  if (strcmp("tail", func) == 0) {
    return builtin_tail(args);
  }
  if (strcmp("concat", func) == 0) {
    return builtin_concat(args);
  }
  if (strcmp("eval", func) == 0) {
    return builtin_eval(args);
  }
  return builtin_op(args, func);
}

lval* builtin_op(lval* args, char* op) {
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

/* Given a qexpr will return the head (aka first) expression */
lval* builtin_head(lval* args) {
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
lval* builtin_tail(lval* args) {
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
lval* builtin_array(lval* args) {
  args->type = LVAL_QEXPR;
  return args;
}

/* Will convert a qexpr into a sexpr and eval it */
lval* builtin_eval(lval* args) {
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
  return eval(x);
}

/* Given a sexpr with multiple qexprs as its children, will combine the qexprs to a single one */
lval* builtin_concat(lval* args) {
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
  }

  free(v);
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
