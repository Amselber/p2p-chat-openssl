// src/cli.h
#ifndef CLI_H
#define CLI_H

typedef struct {
  int success;         // 0 = ошибка, 1 = успех
  const char *message; // статическая строка или NULL
} CommandResult;

typedef CommandResult (*CommandHandler)(int argc, char **argv);

typedef struct {
  const char *name;
  const char *help;
  CommandHandler handler;
} Command;

void cli_register(const Command *cmd);
void cli_run(int argc, char **argv);
void cli_show_help(void);

// Вспомогательные функции (возвращают static)
CommandResult result_success(const char *message);
CommandResult result_error(const char *message);

#endif
