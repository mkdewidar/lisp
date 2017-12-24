#include <stdio.h>

static char input[2048];

int main(int argc, char** argv) {

  puts("Welcome to this (so far) untitled Lisp");
  puts("Press Ctrl+c to exit\n");

  while (1) {
    fputs("lisp> ", stdout);
    fflush(stdout);
    
    char* x = fgets(input, 2048, stdin);

    printf("%s", input);
  }

  return 0;
}
