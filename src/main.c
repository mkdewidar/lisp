#include <stdio.h>
#include "mpc.h"
#include "main.h"

static char input[2048];

// The global environment for the program
env* rootEnv = NULL;

mpc_parser_t* CodeParser;

int main(int argc, char** argv) {

  rootEnv = env_create(NULL);
  if (rootEnv == NULL) {
    fputs("ERROR: Failed to create environment, quitting...", stdout);
    return -1;
  }

  add_all_builtins();

  puts("Welcome to this basic Lisp dialect");
  puts("Press Ctrl+c to exit\n");

  mpc_parser_t* numberParser = mpc_new("number");
  mpc_parser_t* stringParser = mpc_new("string");
  mpc_parser_t* symbolParser = mpc_new("symbol");
  mpc_parser_t* qexprParser = mpc_new("qexpr");
  mpc_parser_t* sexprParser = mpc_new("sexpr");
  mpc_parser_t* exprParser = mpc_new("expr");
  mpc_parser_t* commentParser = mpc_new("comment");
  CodeParser = mpc_new("code");

  mpca_lang(MPCA_LANG_DEFAULT,
	    "number: /-?[0-9]+/; \
            string: /\"(\\\\.|[^\"])*\"/; \
            symbol: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/; \
            qexpr: '[' <expr>* ']'; \
            sexpr: '(' <expr>* ')'; \
            expr: <number> | <string> | <symbol> | <sexpr> | <qexpr> | <comment>; \
            code: /^/ <expr>* /$/; \
            comment: /;[^\\r\\n]*/;",
	    numberParser, stringParser, symbolParser, qexprParser,
	    sexprParser, exprParser, CodeParser, commentParser);

  while (1) {
    fputs("lisp> ", stdout);
    fflush(stdout);

    fgets(input, 2048, stdin);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, CodeParser, &r)) {
      // TODO: These print statements can be hidden behind debug flags
      // mpc_ast_print(r.output);
      lval* expr = read(r.output);
      // lval_print_expr(expr, '(', ')');
      // putchar('\n');
      // fflush(stdout);
      
      expr = eval(rootEnv, expr);
      lval_println(expr);

      lval_del(expr);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

  env_delete(rootEnv);
  mpc_cleanup(8, numberParser, stringParser, symbolParser, sexprParser, qexprParser, exprParser, CodeParser, commentParser);

  return 0;
}

lval* read(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    errno = 0;
    long num = strtol(tree->contents, NULL, 10);
    return (errno == ERANGE) ? lval_err(ERROR_READ_BAD_NUM) : lval_num(num);
  }

  if (strstr(tree->tag, "string")) {
    char* unescaped = malloc(strlen(tree->contents) - 1);
    // we want to ignore the surrounding quotes, so setting the closing quote
    // to be the null terminator, and copying from the second character
    tree->contents[strlen(tree->contents) - 1] = '\0';
    strcpy(unescaped, tree->contents + 1);
    unescaped = mpcf_unescape(unescaped);
    lval* s = lval_str(unescaped);
    free(unescaped);
    return s;
  }

  if (strstr(tree->tag, "symbol")) {
    return lval_sym(tree->contents);
  }

  lval* parentExpression;

  if ((strcmp(tree->tag, ">") == 0) || strstr(tree->tag, "sexpr")) {
    parentExpression = lval_sexpr();
  }
  if (strstr(tree->tag, "qexpr")) {
    parentExpression = lval_qexpr();
  }

  for (int i = 0; i < tree->children_num; i++) {
    if ((strcmp(tree->children[i]->tag, "char") == 0) ||
	(strcmp(tree->children[i]->tag, "regex") == 0) ||
	strstr(tree->children[i]->tag, "comment")) {
      continue;
    }
    lval_add(parentExpression, read(tree->children[i]));
  }

  return parentExpression;
}

lval* eval(env* e, lval* expr) {
  if (expr->type == LVAL_SYM) {
    lval* v = env_get(e, expr);
    lval_del(expr);
    return v;
  }
  if (expr->type == LVAL_SEXPR) {
    return eval_sexpr(e, expr);
  }
  return expr;
}

lval* eval_sexpr(env* e, lval* sexpr) {
  for (int i = 0; i < sexpr->count; i++) {
    sexpr->exprs[i] = eval(e, sexpr->exprs[i]);
  }

  for (int i = 0; i < sexpr->count; i++) {
    if (sexpr->exprs[i]->type == LVAL_ERR) {
      return lval_take(sexpr, i);
    }
  }

  if (sexpr->count == 0) {
    return sexpr;
  }

  if (sexpr->count == 1) {
    return lval_take(sexpr, 0);
  }

  lval* firstExpr = lval_pop(sexpr, 0);
  if (firstExpr->type != LVAL_FUNC) {
    lval_del(firstExpr);
    lval_del(sexpr);
    return lval_err(ERROR_EVAL_INVALID_SEXPR);
  }

  lval* funcReturn = call(e, firstExpr, sexpr);
  lval_del(firstExpr);
  return funcReturn;
}

lval* call(env* e, lval* function, lval* args) {
  if (function->builtin != NULL) {
    return function->builtin(e, args);
  }

  ASSERT_TRUE_OR_RETURN(function->params->count == args->count, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"call", function->params->count, args->count);

  while (args->count) {
    lval* param = lval_pop(function->params, 0);
    lval* value = lval_pop(args, 0);

    env_put(function->scope, param->symbol, value);

    lval_del(param);
    lval_del(value);
  }

  lval_del(args);

  if (function->params->count == 0) {
    lval* body = lval_sexpr();
    lval_add(body, lval_copy(function->body));
    return builtin_eval(function->scope, body);
  } else {
    // a "partial" function
    return lval_copy(function);
  }
}

void add_builtin(char* identifier, lbuiltin func) {
  lval* f = lval_func(func);
  env_put(rootEnv, identifier, f);
  lval_del(f);
}

void add_all_builtins() {
  add_builtin("array", builtin_array);
  add_builtin("head", builtin_head);
  add_builtin("tail", builtin_tail);
  add_builtin("concat", builtin_concat);
  add_builtin("eval", builtin_eval);
  add_builtin("def", builtin_def);
  add_builtin("\\", builtin_lambda);
  add_builtin("if", builtin_if);

  add_builtin("!", builtin_not);

  add_builtin(">", builtin_gt);
  add_builtin(">=", builtin_gte);
  add_builtin("<", builtin_lt);
  add_builtin("<=", builtin_lte);
  add_builtin("==", builtin_eq);

  add_builtin("+", builtin_add);
  add_builtin("-", builtin_sub);
  add_builtin("*", builtin_mul);
  add_builtin("/", builtin_div);

  add_builtin("load", builtin_load);
  add_builtin("print", builtin_print);
  add_builtin("error", builtin_error);
}

lval* builtin_op(env* e, lval* args, char* operator) {
  for (int i = 0; i < args->count; i++) {
    ASSERT_TRUE_OR_RETURN(args->exprs[i]->type == LVAL_NUM, args,
			  "Expected numbers as arguments for calculation, got %s",
			  lval_typename(args->exprs[i]->type));
  }

  lval* x = lval_pop(args, 0);

  if ((strcmp(operator, "-") == 0) && (args->count == 0)) {
    x->num = - x->num;
  }

  while (args->count > 0) {
    lval* y = lval_pop(args, 0);

    if (strcmp(operator, "+") == 0) {
      x->num += y->num;
    }
    if (strcmp(operator, "-") == 0) {
      x->num -= y->num;
    }
    if (strcmp(operator, "*") == 0) {
      x->num *= y->num;
    }
    if (strcmp(operator, "/") == 0) {
      if (y->num == 0) {
	lval_del(x);
	lval_del(y);
	x = lval_err(ERROR_DIV_BY_ZERO);
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
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"head", 1, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"head", 1,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[0]->type));

  lval* firstExpr = lval_take(args->exprs[0], 0);
  lval_del(args);
  return firstExpr;
}

/* Given a qexpr will return the tail (aka last) expression */
lval* builtin_tail(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"tail", 1, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"tail", 1,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[0]->type));

  lval* lastExpr = lval_take(args->exprs[0], args->exprs[0]->count - 1);
  lval_del(args);
  return lastExpr;
}

/* Converts a sexpr into a qexpr */
lval* builtin_array(env* e, lval* args) {
  args->type = LVAL_QEXPR;
  return args;
}

/* Will convert a qexpr into a sexpr and eval it */
lval* builtin_eval(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"eval", 1, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"tail", 1,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[0]->type));

  lval* qexpr = lval_pop(args, 0);
  lval_del(args);
  qexpr->type = LVAL_SEXPR;
  return eval(e, qexpr);
}

/* Given a sexpr with multiple qexprs as its children, will combine the qexprs to a single one */
lval* builtin_concat(env* e, lval* args) {
  for (int i = 0; i < args->count; i++) {
    ASSERT_TRUE_OR_RETURN(args->exprs[i]->type == LVAL_QEXPR, args,
			  T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			  "tail", i + 1,
			  lval_typename(LVAL_QEXPR), lval_typename(args->exprs[i]->type));
  }

  lval* finalQexpr = lval_pop(args, 0);

  while (args->count) {
    while (args->exprs[0]->count) {
      lval_add(finalQexpr, lval_pop(args->exprs[0], 0));
    }
    lval_del(lval_pop(args, 0));
  }

  lval_del(args);
  return finalQexpr;
}

lval* builtin_def(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"def", 1,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[0]->type));

  // first arg is a qexpr of the names of the identifiers
  // the remaining args are the values to be mapped onto them
  lval* identifiers = args->exprs[0];

  for (int i = 0; i < identifiers->count; i++) {
    ASSERT_TRUE_OR_RETURN(identifiers->exprs[i]->type == LVAL_SYM, args,
			  T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			  "def", i + 1,
			  lval_typename(LVAL_SYM), lval_typename(args->exprs[i]->type));
  }

  ASSERT_TRUE_OR_RETURN(identifiers->count == (args->count - 1), args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"def", identifiers->count, (args->count - 1));

  for (int i = 0; i < identifiers->count; i++) {
    env_put(e, identifiers->exprs[i]->symbol, args->exprs[i + 1]);
  }

  lval_del(args);
  return lval_sexpr();
}

lval* builtin_lambda(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 2, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"lambda", 2, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"lambda", 1,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[0]->type));

  ASSERT_TRUE_OR_RETURN(args->exprs[1]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"lambda", 2,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[1]->type));

  for (int i = 0; i < args->exprs[0]->count; i++) {
    ASSERT_TRUE_OR_RETURN(args->exprs[0]->exprs[i]->type == LVAL_SYM, args,
			  T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			  "lambda", i + 1,
			  lval_typename(LVAL_SYM),
			  lval_typename(args->exprs[0]->exprs[i]->type));
  }

  lval* params = lval_pop(args, 0);
  lval* body = lval_pop(args, 0);
  lval_del(args);

  return lval_lambda(e, params, body);
}

lval* builtin_load(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"load", 1, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_STR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"load", 1,
			lval_typename(LVAL_STR), lval_typename(args->exprs[0]->type));

  lval* loadResult;

  mpc_result_t module;
  if (mpc_parse_contents(args->exprs[0]->str, CodeParser, &module)) {
    lval* expr = read(module.output);
    mpc_ast_delete(module.output);

    while (expr->count) {
      lval* x = eval(e, lval_pop(expr, 0));
      // this way, we can print a single error per statement in the module
      if (x->type == LVAL_ERR) {
	lval_println(x);
      }
      lval_del(x);
    }

    lval_del(expr);

    loadResult = lval_sexpr();
  } else {
    char* e = mpc_err_string(module.error);
    mpc_err_delete(module.error);

    loadResult = lval_err(e);
    free(e);
  }

  lval_del(args);

  return loadResult;
}

lval* builtin_print(env* e, lval* args) {
  for (int i = 0; i < args->count; i++) {
    lval_print(args->exprs[i]);
    putchar(' ');
  }
  putchar('\n');

  lval_del(args);
  return lval_sexpr();
}

lval* builtin_error(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"error", 1, args->count);

  ASSERT_TRUE_OR_RETURN(args->exprs[0]->type == LVAL_STR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"error", 1,
			lval_typename(LVAL_STR), lval_typename(args->exprs[0]->type));

  lval* error = lval_err(args->exprs[0]->str);

  lval_del(args);
  return error;
}

lval* builtin_not(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 1, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"!", 1, args->count);

  lval* inversedValue;
  if (is_truthy(e, args->exprs[0])) {
    inversedValue = lval_num(0);
  } else {
    inversedValue = lval_num(1);
  }

  lval_del(args);
  return inversedValue;
}

lval* builtin_cmp(env* e, lval* args, char* op) {
  ASSERT_TRUE_OR_RETURN(args->count == 2, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			op, 2, args->count);

  ASSERT_TRUE_OR_RETURN((args->exprs[0]->type == LVAL_NUM),
			args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			op, 1,
			lval_typename(LVAL_NUM), lval_typename(args->exprs[0]->type));
  ASSERT_TRUE_OR_RETURN((args->exprs[1]->type == LVAL_NUM),
			args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			op, 2,
			lval_typename(LVAL_NUM), lval_typename(args->exprs[1]->type));

  int result = 0;
  if (strcmp(op, ">") == 0) {
    result = (args->exprs[0]->num > args->exprs[1]->num);
  } else if (strcmp(op, ">=") == 0) {
    result = (args->exprs[0]->num >= args->exprs[1]->num);
  } else if (strcmp(op, "<") == 0) {
    result = (args->exprs[0]->num < args->exprs[1]->num);
  } else if (strcmp(op, "<=") == 0) {
    result = (args->exprs[0]->num <= args->exprs[1]->num);
  }

  return lval_num(result);
}

lval* builtin_gt(env* e, lval* args) {
  return builtin_cmp(e, args, ">");
}

lval* builtin_gte(env* e, lval* args) {
  return builtin_cmp(e, args, ">=");
}

lval* builtin_lt(env* e, lval* args) {
  return builtin_cmp(e, args, "<");
}

lval* builtin_lte(env* e, lval* args) {
  return builtin_cmp(e, args, "<=");
}

lval* builtin_eq(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count == 2, args,
			T_ERROR_FUNC_UNEXPECTED_ARGS_NUM,
			"==", 2, args->count);

  int result = lval_eq(args->exprs[0], args->exprs[1]);
  lval_del(args);
  return lval_num(result);
}

lval* builtin_if(env* e, lval* args) {
  ASSERT_TRUE_OR_RETURN(args->count >= 2, args,
			"Function %s expected at least %d arguments recieved %d",
			"if", 2, args->count);

  ASSERT_TRUE_OR_RETURN(args->count <= 3, args,
			"Function %s expected no more than %d arguments recieved %d",
			"if", 3, args->count);

  if (args->exprs[0]->type == LVAL_ERR) {
    return lval_take(args, 0);
  }

  // the expressions to execute based on the condition have
  // to be qexprs
  ASSERT_TRUE_OR_RETURN(args->exprs[1]->type == LVAL_QEXPR, args,
			T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			"if", 2,
			lval_typename(LVAL_QEXPR), lval_typename(args->exprs[1]->type));
  if (args->count == 3) {
    ASSERT_TRUE_OR_RETURN(args->exprs[2]->type == LVAL_QEXPR, args,
			  T_ERROR_FUNC_INCORRECT_ARG_TYPE,
			  "if", 3,
			  lval_typename(LVAL_QEXPR), lval_typename(args->exprs[2]->type));
  }

  lval* result;

  args->exprs[1]->type = LVAL_SEXPR;
  args->exprs[2]->type = LVAL_SEXPR;
  
  if (is_truthy(e, args->exprs[0])) {
    result = eval(e, lval_take(args, 1));
  } else if (args->count == 3) {
    result = eval(e, lval_take(args, 2));
  } else {
    result = lval_sexpr();
  }

  lval_del(args);
  return result;
}

int is_truthy(env* e, lval* val) {
  int truthy = 0;

  // These are the only three types that we have to account for
  // as a nature of how the interpreter works, everything else would've
  // been evaluated into these.
  switch(val->type) {
  case LVAL_NUM:
    return val->num;

  case LVAL_QEXPR:
    return val->count > 0;

  case LVAL_FUNC:
    // Lambdas are always true since they are "something"
    // and if they've made it this far they're syntactically correct
    return 1;
  }

  return truthy;
}

lval* lval_num(long num) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = num;
  return v;
}

lval* lval_str(char* str) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(str) + 1);
  strcpy(v->str, str);
  return v;
}

lval* lval_err(char* msgFormat, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  va_list va;
  va_start(va, msgFormat);

  v->error = malloc(512);
  vsnprintf(v->error, 511, msgFormat, va);
  v->error = realloc(v->error, strlen(v->error) + 1);

  va_end(va);
  return v;
}

lval* lval_sym(char* identifier) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->symbol = malloc(strlen(identifier) + 1);
  strcpy(v->symbol, identifier);
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

lval* lval_lambda(env* parentEnv, lval* params, lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;
  v->builtin = NULL;
  
  v->scope = env_create(parentEnv);

  v->params = params;
  v->body = body;
  return v;
}

lval* lval_take(lval* parentExpr, int index) {
  lval* childVal = lval_pop(parentExpr, index);
  lval_del(parentExpr);
  return childVal;
}

/* Given an expr will take out the element at index i of the subexpressions */
lval* lval_pop(lval* parentExpr, int index) {
  lval* childVal = parentExpr->exprs[index];

  memmove(&parentExpr->exprs[index], &parentExpr->exprs[index + 1],
	  sizeof(lval*) * (parentExpr->count - index - 1));

  parentExpr->count--;

  // This frees the last memory location that we no longer need when we did the memmove
  parentExpr->exprs = realloc(parentExpr->exprs,
			      sizeof(lval*) * parentExpr->count);
  return childVal;
}

void lval_add(lval* sexpr, lval* val) {
  sexpr->count++;
  sexpr->exprs = realloc(sexpr->exprs, sizeof(lval*) * sexpr->count);
  sexpr->exprs[sexpr->count - 1] = val;
}

void lval_del(lval* val) {
  switch(val->type) {
  case LVAL_NUM:
    break;

  case LVAL_STR:
    free(val->str);

  case LVAL_ERR:
    free(val->error);
    break;

  case LVAL_SYM:
    free(val->symbol);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    for (int i = 0; i < val->count; i++) {
      lval_del(val->exprs[i]);
    }
    free(val->exprs);
    break;

  case LVAL_FUNC:
    if (!val->builtin) {
      lval_del(val->params);
      lval_del(val->body);
      env_delete(val->scope);
    }
    break;
  }

  free(val);
}

lval* lval_copy(lval* val) {
  lval* copy = malloc(sizeof(lval));
  copy->type = val->type;

  switch(val->type) {
  case LVAL_NUM:
    copy->num = val->num;
    break;

  case LVAL_STR:
    copy->str = malloc(strlen(val->str) + 1);
    strcpy(copy->str, val->str);
    break;

  case LVAL_ERR:
    copy->error = malloc(strlen(val->error) + 1);
    strcpy(copy->error, val->error);
    break;

  case LVAL_SYM:
    copy->symbol = malloc(strlen(val->symbol) + 1);
    strcpy(copy->symbol, val->symbol);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    copy->count = val->count;
    copy->exprs = malloc(sizeof(lval*) * val->count);
    for (int i = 0; i < val->count; i++) {
      copy->exprs[i] = lval_copy(val->exprs[i]);
    }
    break;

  case LVAL_FUNC:
    if (val->builtin != NULL) {
      copy->builtin = val->builtin;
    } else {
      copy->builtin = NULL;
      copy->params = lval_copy(val->params);
      copy->body = lval_copy(val->body);
      copy->scope = env_copy(val->scope);
    }
    break;
  }

  return copy;
}

int lval_eq(lval* a, lval* b) {
  if (a->type != b->type) {
    return 0;
  }

  switch(a->type) {
  case LVAL_NUM:
    return (a->num == b->num);

  case LVAL_STR:
    return (strcmp(a->str, b->str) == 0);

  case LVAL_SYM:
    return (strcmp(a->symbol, b->symbol) == 0);

  case LVAL_ERR:
    return (strcmp(a->error, b->error) == 0);

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    if (a->count != b->count) {
      return 0;
    }
    
    for (int i = 0; i < a->count; i++) {
      if (lval_eq(a->exprs[i], b->exprs[i]) == 0) {
	return 0;
      }
    }

    return 1;

  case LVAL_FUNC:
    if (a->builtin != b->builtin) {
      return 0;
    }
    if (lval_eq(a->body, b->body) == 0) {
      return 0;
    }
    if (lval_eq(a->params, b->params) == 0) {
      return 0;
    }
    return 1;
  }
}

void lval_print(lval* val) {
  switch(val->type) {
  case LVAL_NUM:
    printf("%li", val->num);
    break;

  case LVAL_STR: {
      char* escaped = malloc(strlen(val->str) + 1);
      strcpy(escaped, val->str);
      escaped = mpcf_escape(escaped);
      printf("\"%s\"", escaped);
      free(escaped);
      break;
    }

  case LVAL_ERR:
    printf("Error: %s", val->error);
    break;

  case LVAL_SYM:
    printf("%s", val->symbol);
    break;

  case LVAL_SEXPR:
    lval_print_expr(val, '(', ')');
    break;

  case LVAL_QEXPR:
    lval_print_expr(val, '[', ']');
    break;

  case LVAL_FUNC:
    if (val->builtin) {
      printf("<function>");
    } else {
      printf("(\\ ");
      lval_print(val->params);
      putchar(' ');
      lval_print(val->body);
      putchar(')');
    }
    break;
  }
}

void lval_println(lval* val) {
  lval_print(val);
  putchar('\n');
}

void lval_print_expr(lval* val, char openChar, char closeChar) {
  putchar(openChar);

  for (int i = 0; i < val->count; i++) {
    lval_print(val->exprs[i]);

    if (i != (val->count - 1)) {
      putchar(' ');
    }
  }

  putchar(closeChar);
}

char* lval_typename(int typeEnum) {
  switch(typeEnum) {
  case LVAL_ERR:
    return "Error";
  case LVAL_NUM:
    return "Number";
  case LVAL_STR:
    return "String";
  case LVAL_SYM:
    return "Symbol";
  case LVAL_FUNC:
    return "Function";
  case LVAL_SEXPR:
    return "S-Expression";
  case LVAL_QEXPR:
    return "Q-Expression";
  default:
    return "Unknown";
  }
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
    return lval_err(T_ERROR_UNDEFINED_SYMBOL, key->symbol);
  }
}
