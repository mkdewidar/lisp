#include <stdio.h>
#include "mpc.h"

static char input[2048];

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
      mpc_ast_print(r.output);     
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }
  }

  mpc_cleanup(4, number, operator, expr, code);

  return 0;
}
