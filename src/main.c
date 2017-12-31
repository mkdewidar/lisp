#include <stdio.h>
#include "mpc.h"

static char input[2048];

typedef struct {
  int type;
  union {
    long num;
    int error;
  };
} lval;

lval eval(mpc_ast_t* tree);
lval eval_op(char* operator, lval accumulate, lval operand);

enum { LVAL_NUM, LVAL_ERR };
lval lval_num(long x);
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };
lval lval_err(int x);
void lval_print(lval v);
void lval_println(lval v);


int main(int argc, char** argv) {

  puts("Welcome to this (so far) untitled Lisp");
  puts("Press Ctrl+c to exit\n");

  mpc_parser_t* number = mpc_new("number");
  mpc_parser_t* operator = mpc_new("operator");
  mpc_parser_t* expr = mpc_new("expr");
  mpc_parser_t* code = mpc_new("code");

  mpca_lang(MPCA_LANG_DEFAULT,
	    "number: /-?[0-9]+/; \
             operator: '+' | '-' | '*' | '/'; \
             expr: <number> | '(' <operator> <expr>+ ')'; \
             code: /^/ <operator> <expr>+ /$/;",
	    number, operator, expr, code);


  while (1) {
    fputs("lisp> ", stdout);
    fflush(stdout);

    char* x = fgets(input, 2048, stdin);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, code, &r)) {
      // TODO: hide this behind a debug flag?
      // mpc_ast_print(r.output);
      lval_println(eval(r.output));

      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

  mpc_cleanup(4, number, operator, expr, code);

  return 0;
}

lval eval(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    errno = 0;
    long num = strtol(tree->contents, NULL, 10);
    return (errno == ERANGE) ? lval_err(LERR_BAD_NUM) : lval_num(num);
  }

  char* operator = tree->children[1]->contents;
  lval accumulate = eval(tree->children[2]);

  int childIndex = 3;
  while (strstr(tree->children[childIndex]->tag, "expr")) {
    accumulate = eval_op(operator, accumulate, eval(tree->children[childIndex]));
    childIndex++;
  }

  return accumulate;
}

lval eval_op(char* operator, lval accumulate, lval operand) {
  if (accumulate.type == LVAL_ERR) return accumulate;
  if (operand.type == LVAL_ERR) return operand;

  if (strcmp(operator, "+") == 0) {
    return lval_num(accumulate.num + operand.num);
  }
  if (strcmp(operator, "-") == 0) {
    return lval_num(accumulate.num - operand.num);
  }
  if (strcmp(operator, "*") == 0) {
    return lval_num(accumulate.num * operand.num);
  }
  if (strcmp(operator, "/") == 0) {
    return (operand.num == 0) ? lval_err(LERR_DIV_ZERO)
                              : lval_num(accumulate.num / operand.num);
  }

  return lval_err(LERR_BAD_OP);
}

lval lval_num(long n) {
  lval v;
  v.type = LVAL_NUM;
  v.num = n;
  return v;
}

lval lval_err(int code) {
  lval v;
  v.type = LVAL_ERR;
  v.error = code;
  return v;
}

void lval_print(lval v) {
  switch(v.type) {
  case LVAL_NUM:
    printf("%li", v.num);
    break;

  case LVAL_ERR:
    if (v.error == LERR_DIV_ZERO) {
      printf("Error: Division By Zero!");
    }
    if (v.error == LERR_BAD_OP) {
      printf("Error: Invalid Operator!");
    }
    if (v.error == LERR_BAD_NUM) {
      printf("Error: Invalid Number!");
    }
    break;
  }
}
void lval_println(lval v) {
  lval_print(v);
  putchar('\n');
}
