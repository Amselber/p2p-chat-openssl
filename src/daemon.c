// src/daemon.c
#include "daemon.h"
#include "cli.h"
#include "log.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

static volatile int running = 1;
static int epfd;

// Обработчик Ctrl+C
static void on_signal(int s) {
  (void)s;
  log_info("Signal %d received, shutting down", s);
  running = 0;
}

// CLI Commands
// Exit
static CommandResult cmd_quit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_info("User requested quit");
  running = 0;
  return result_success(NULL);
}

// Help
static CommandResult cmd_help(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_show_help();
  return result_success(NULL);
}

// Echo
static CommandResult cmd_echo(int argc, char **argv) {
  if (argc < 1)
    return result_error("Usage: /echo <text>");
  static char buf[256];
  buf[0] = 0;
  for (int i = 0; i < argc; i++) {
    if (i > 0)
      strcat(buf, " ");
    strcat(buf, argv[i]);
  }
  return result_success(buf);
}

// Регистрация всех команд
static void register_commands(void) {
  static Command cmds[] = {{"quit", "Exit", cmd_quit},
                           {"help", "Help", cmd_help},
                           {"echo", "Echo", cmd_echo},
                           {NULL, NULL, NULL}};

  int i;
  for (i = 0; cmds[i].name; ++i)
    cli_register(&cmds[i]);

  log_debug("Registered %d commands", i);
}

// Запуск демона
int daemon_run(void) {
  log_info("Daemon starting");
  // SIGINT SIGTERM register handler
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  // Регистрация команд
  register_commands();

  // Экземпляр epoll
  epfd = epoll_create1(0);
  if (epfd < 0) {
    log_errno("epoll_create1");
    return 1;
  }

  // События epoll для наблюдения за fd
  // EPOLLIN пробуждение при появлении данных для чтения
  struct epoll_event ev = {.events = EPOLLIN};

  // Неблокирующий stdin (не требуется нажатия enter)
  // Чтение флагов
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (flags == -1) {
    log_errno("fcntl F_GETFL");
    close(epfd);
    return 1;
  }

  // Установка non block
  if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_errno("fcntl O_NONBLOCK failed");
    close(epfd);
    return 1;
  }

  // Привязываем fd 0 (stdin) к полю data.fd структуры ev.
  // Когда epoll_wait проснётся, мы узнаем, какой fd сработал, через
  // events[i].data.fd.
  ev.data.fd = STDIN_FILENO;

  // Добавляем stdin в наблюдение epoll.
  // EPOLL_CTL_ADD = «добавить новый fd», STDIN_FILENO = 0, &ev = настройки.
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
    log_errno("epoll_ctl ADD stdin failed");
    close(epfd);
    return 1;
  }

  // evs — массив, куда epoll_wait запишет сработавшие события.
  // buf — буфер для накопления ввода до '\n'.
  // pos — текущая позиция записи в buf.
  struct epoll_event evs[1];
  char buf[256];
  int pos = 0;

  log_debug("Entering main loop");

  // Help
  cli_show_help();

  // Приглашение и сброс буфера
  printf("> ");
  fflush(stdout);

  // main loop
  while (running) {
    // epoll_wait блокируется до события или сигнала.
    // evs — куда записать события, 1 — размер массива событий, -1 — ждать
    // бесконечно. Возвращает количество готовых fd или -1 при ошибке.
    int n = epoll_wait(epfd, evs, 1, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_errno("epoll_wait failed");
      break;
    }

    char c;
    // Читаем stdin посимвольно. Неблокирующий — read вернёт 0 при EOF или -1
    // если нет данных.
    while (read(STDIN_FILENO, &c, 1) > 0) {
      if (c == '\n') {
        // Достигли конца строки — завершаем буфер.
        buf[pos] = 0;

        if (pos > 0) {
          log_debug("Input: '%s'", buf);

          if (buf[0] == '/') {
            char *av[16];
            int ac = 1;
            av[0] = "p2pchat";

            // Разбор строки
            char *p = buf + 1;
            while (*p) {
              // Пропускаем пробелы перед аргументами
              while (*p == ' ')
                ++p;
              if (!*p) // Конец строки
                break;
              av[ac++] = p; // Запоминаем начало аргумента
              // Ищем конец аргемента (пробел или конец строки)
              while (*p && *p != ' ')
                ++p;
              if (*p)
                *p++ = 0; // Заменяем пробел на '\0'
            }
            // Например: "/msg abc123 привет"
            // После разбора:
            //   buf = "/msg\0abc123\0привет"
            //   av  = ["p2pchat", "msg", "abc123", "привет"]
            //   ac  = 4

            log_debug("Running command: %s (%d args)", av[1], ac - 1);
            cli_run(ac, av); // Выполняем команду
          } else {
            printf("You typed: %s\n", buf);
          }
        }

        // Сбрасываем буфер для следующей строки
        pos = 0;
        if (running) {
          // Печатаем новое приглашение, если не вышли.
          printf("> ");
          fflush(stdout);
        }
      } else if (pos < (int)sizeof(buf) - 1) {
        // Не конец строки и есть место в буфере — добавляем символ.
        buf[pos++] = c;
      }
      // Если буфер полон (pos == sizeof(buf)-1), символы молча отбрасываются.
    }
    // если pos == sizeof(buf)-1, символы игнорируются
  }

  close(epfd);
  log_info("Daemon stopped");
  return 0;
}
