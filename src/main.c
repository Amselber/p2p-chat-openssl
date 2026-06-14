// src/main.c
#include "cli.h"
#include "commands.h"

int main(int argc, char **argv) {
  register_all_commands();
  cli_run(argc, argv);

  return 0;
}
