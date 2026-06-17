// src/main.c
#include "cli.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "help")) {
    cli_show_help();
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "version")) {
    printf("0.0.1\n");
  }

  return 0;
}
