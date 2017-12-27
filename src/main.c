#include <stdio.h>
#include "mpc.h"

static char input[2048];

long eval(mpc_ast_t* tree);
long eval_op(char* operator, long accumulate, long operand);

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
      printf("%li\n", eval(r.output));

      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

  mpc_cleanup(4, number, operator, expr, code);

  return 0;
}

long eval(mpc_ast_t* tree) {
  if (strstr(tree->tag, "number")) {
    return atoi(tree->contents);
  }

  char* operator = tree->children[1]->contents;
  long accumulate = eval(tree->children[2]);

  int childIndex = 3;
  while (strstr(tree->children[childIndex]->tag, "expr")) {
    accumulate = eval_op(operator, accumulate, eval(tree->children[childIndex]));
    childIndex++;
  }
  
  return accumulate;
}

long eval_op(char* operator, long accumulate, long operand) {
  if (strcmp(operator, "+") == 0) {
    return accumulate + operand;
  }
  if (strcmp(operator, "-") == 0) {
    return accumulate - operand;
  }
  if (strcmp(operator, "*") == 0) {
    return accumulate * operand;
  }
  if (strcmp(operator, "/") == 0) {
    return accumulate / operand;
  }

  return 0;
}
