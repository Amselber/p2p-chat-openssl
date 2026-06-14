// src/cli.h - интерфейс команд
#ifndef CLI_H
#define CLI_H

// Результат выполнения команды
typedef struct {
  int success;         // 0 = ошибка, 1 = успех
  const char *message; // сообщение для пользователя
} CommandResult;

// Тип функции-обработчика команды
typedef CommandResult *(*CommandHandler)(int argc, char **argv);

// Структура команды
typedef struct {
  const char *name;
  const char *help;
  CommandHandler handler;
} Command;

// Регистрация и выполнение команд
void cli_register(const Command *cmd);
void cli_run(int argc, char **argv);

// Вспомогательные функции для создания результатов
CommandResult *result_success(const char *message);
CommandResult *result_error(const char *message);

// Освобождение результата
void result_free(CommandResult *result);

#endif
