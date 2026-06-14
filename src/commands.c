// src/commands.c
#include "cli.h"
#include <stdio.h>
#include <string.h>

// !!! argc = agrc - 2
// !!! argv = argv + 2

// Команда Echo (для тестирования)
static CommandResult *cmd_echo(int argc, char **argv) {
  if (argc < 1) {
    return result_error("Usage: echo <text>");
  }

  // Статический буфер для работы Echo
  static char echo_buffer[1024] = {0};
  echo_buffer[0] = '\0';

  for (int i = 0; i < argc; i++) {
    strcat(echo_buffer, argv[i]);
    if (i < argc - 1)
      strcat(echo_buffer, " ");
  }
  return result_success(echo_buffer);
}

// Команда: help - вывести подсказку
static CommandResult *cmd_help(int argc, char **argv) {
  // Статический буфер для сообщений help
  static char help_msg[2048];

  if (argc >= 1) {
    // Справка по конкретной команде
    const char *cmd = argv[0];

    if (strcmp(cmd, "echo") == 0) {
      return result_success("Usage: echo <text>\nExample: echo hello world");
    }

    snprintf(help_msg, sizeof(help_msg), "Unknown command: %s", cmd);
    return result_error(help_msg);
  }

  // Общая справка
  snprintf(help_msg, sizeof(help_msg),
           "P2P Chat CLI - Available commands:\n\n"
           "  echo <text>       Echo text\n"
           "  help [cmd]        Show this help or help for command\n");

  return result_success(help_msg);
}

// Регистрация команд в массиве
static const Command COMMANDS[] = {{"echo", "Echo text", cmd_echo},
                                   {"help", "Print help", cmd_help},
                                   {NULL, NULL, NULL}};

void register_all_commands(void) {
  for (int i = 0; COMMANDS[i].name != NULL; ++i) {
    cli_register(&COMMANDS[i]);
  }
}
