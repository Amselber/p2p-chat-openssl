// src/cli.c - движок команд
#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMMANDS 32

static Command commands[MAX_COMMANDS];
static int command_count = 0;

void cli_register(const Command *cmd) {
  if (command_count < MAX_COMMANDS) {
    commands[command_count++] = *cmd;
  }
}

CommandResult *result_success(const char *message) {
  CommandResult *r = malloc(sizeof(CommandResult));
  r->success = 1;
  r->message = message;
  return r;
}

CommandResult *result_error(const char *message) {
  CommandResult *r = malloc(sizeof(CommandResult));
  r->success = 0;
  r->message = message;
  return r;
}

void result_free(CommandResult *r) {
  if (r) {
    free(r);
  }
}

static void show_help(const char *prog_name) {
  printf("Usage: %s <command> [args...]\n\n", prog_name);
  printf("Commands:\n");
  for (int i = 0; i < command_count; i++) {
    printf("  %-12s %s\n", commands[i].name, commands[i].help);
  }
}

void cli_run(int argc, char **argv) {
  if (argc < 2) {
    show_help(argv[0]);
    return;
  }

  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].name, argv[1]) == 0) {
      // Вызов обработчика
      CommandResult *result = commands[i].handler(argc - 2, argv + 2);

      // Вывод результата
      if (result->success) {
        if (result->message)
          printf("%s\n", result->message);
      } else {
        fprintf(stderr, "Error: %s\n", result->message);
      }

      result_free(result);
      return;
    }
  }

  // Команда не найдена
  fprintf(stderr, "Unknown command: %s\n", argv[1]);
  show_help(argv[0]);
}
