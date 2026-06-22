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

static void on_signal(int s) {
  (void)s;
  log_info("Signal %d received, shutting down", s);
  running = 0;
}

// CLI Commands
static CommandResult cmd_quit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_info("User requested quit");
  running = 0;
  return result_success(NULL);
}
static CommandResult cmd_help(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_show_help();
  return result_success(NULL);
}

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

int daemon_run(void) {
  log_info("Daemon starting");
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  register_commands();

  epfd = epoll_create1(0);
  if (epfd < 0) {
    log_errno("epoll_create1");
    return 1;
  }

  struct epoll_event ev = {.events = EPOLLIN};

  if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0) {
    log_errno("fcntl O_NONBLOCK failed");
    close(epfd);
    return 1;
  }

  ev.data.fd = STDIN_FILENO;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
    log_errno("epoll_ctl ADD stdin failed");
    close(epfd);
    return 1;
  }

  struct epoll_event evs[1];
  char buf[256];
  int pos = 0;

  log_debug("Entering main loop");

  cli_show_help();
  printf("> ");
  fflush(stdout);
  while (running) {
    int n = epoll_wait(epfd, evs, 1, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_errno("epoll_wait failed");
      break;
    }

    char c;
    while (read(STDIN_FILENO, &c, 1) > 0) {
      if (c == '\n') {
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
              while (*p == ' ')
                ++p;
              if (!*p)
                break;
              av[ac++] = p;
              while (*p && *p != ' ')
                ++p;
              if (*p)
                *p++ = 0;
            }

            log_debug("Running command: %s (%d args)", av[1], ac - 1);
            cli_run(ac, av);
          } else {
            printf("You typed: %s\n", buf);
          }
        }

        pos = 0;
        if (running) {
          printf("> ");
          fflush(stdout);
        }
      } else if (pos < (int)sizeof(buf) - 1) {
        buf[pos++] = c;
      }
    }
  }

  close(epfd);
  log_info("Daemon stopped");
  return 0;
}
