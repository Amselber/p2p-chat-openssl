// src/cli.c
#include "cli.h"
#include <stdio.h>
#include <string.h>

#define MAX_CMDS 32
static Command cmds[MAX_CMDS];
static int ncmds = 0;

void cli_register(const Command *c) {
  if (ncmds < MAX_CMDS)
    cmds[ncmds++] = *c;
}

CommandResult result_success(const char *m) {
  CommandResult r = {1, m};
  return r;
}

CommandResult result_error(const char *m) {
  CommandResult r = {0, m};
  return r;
}

void cli_show_help(void) {
  printf("Commands:\n");
  for (int i = 0; i < ncmds; i++) {
    printf("  /%-12s %s\n", cmds[i].name, cmds[i].help);
  }
}

void cli_run(int argc, char **argv) {
  if (argc < 2)
    return;

  for (int i = 0; i < ncmds; i++) {
    if (!strcmp(cmds[i].name, argv[1])) {
      CommandResult r = cmds[i].handler(argc - 2, argv + 2);
      if (r.success) {
        if (r.message)
          printf("%s\n", r.message);
      } else {
        fprintf(stderr, "Error: %s\n", r.message);
      }
      return;
    }
  }

  fprintf(stderr, "Unknown: %s\n", argv[1]);
}
