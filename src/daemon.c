// src/daemon.c
/*
 * daemon.c — главный цикл P2P-чата
 *
 * Архитектура:
 *   - Один поток, один epoll-цикл
 *   - Обрабатывает stdin, UDP (discovery), TCP (transport)
 *   - Все соединения неблокирующие
 *
 * Порядок инициализации:
 *   1. Сигналы (SIGINT, SIGTERM)
 *   2. Регистрация CLI-команд
 *   3. transport_tls_init() — сертификаты, ключи, fingerprint
 *   4. discovery_init()     — UDP Multicast
 *   5. transport_listen()   — TCP слушающий сокет
 *   6. Первый HELLO
 *   7. Главный цикл epoll
 */
#define _POSIX_C_SOURCE 199309L

#include "daemon.h"
#include "cli.h"
#include "commands.h"
#include "config.h"
#include "connection.h"
#include "discovery.h"
#include "log.h"
#include "msg_store.h"
#include "node.h"
#include "transport.h"
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

/* ─── Константы ─── */
#define MAX_EVENTS 16

/* ─── Глобальные переменные ─── */
volatile int running = 1;
static int epfd;
static int udp_fd;
static int tcp_fd;
static uint16_t tcp_port = 0; // 0 - OS Selected
volatile int general_chat_active = 1;

/* ———  Хелпер: сон в миллисекундах ——— */
static void sleep_ms(int ms) {
  struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

struct broadcast_args {
  const char *text; // текст для отправки
};

/*
 * send_to_node — отправка сообщения одному узлу
 * Вызывается для каждого узла при broadcast.
 * Если отправка не удалась — помечаем узел офлайн (fd = -1).
 */
static void send_to_node(node_t *n, void *arg) {
  struct broadcast_args *a = (struct broadcast_args *)arg;
  if (n->fd != -1) {
    if (transport_send(n->fd, a->text) < 0) {
      log_warn("Send failed to %s (fd=%d)", n->name, n->fd);
      n->fd = -1; // помечаем офлайн
    }
  }
}

/* ─── Обработчик сигналов ─── */

/*
 * on_signal — вызывается при Ctrl+C (SIGINT) или kill (SIGTERM)
 * Устанавливает running = 0, главный цикл корректно завершается.
 */
static void on_signal(int s) {
  (void)s;
  log_info("Signal %d received, shutting down", s);
  running = 0;
}

/*
 * ================================================================
 * ===================== Обработчики FD ===========================
 * ================================================================
 */

// Обработчик ввода
static void handle_stdin(void) {
  static char buf[256];
  static int pos = 0;
  char c;
  // Читаем stdin посимвольно. Неблокирующий — read вернёт 0 при EOF или
  // -1 если нет данных.
  while (read(STDIN_FILENO, &c, 1) > 0) {
    if (c == '\n') {
      // Достигли конца строки — завершаем буфер.
      buf[pos] = 0;

      if (pos > 0) {
        log_debug("Input: '%s'", buf);

        if (buf[0] == '/') {
          char *av[16];
          int ac = 1;
          // Для совместимости
          av[0] = "p2pchat";

          // Разбор строки
          char *p = buf + 1;
          while (*p) {
            // Пропускаем пробелы перед аргументами
            while (*p == ' ')
              p++;
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
          if (general_chat_active) {
            log_debug("Send message to all connected nodes: %s", buf);
            struct broadcast_args ba = {buf};
            node_each(send_to_node, &ba);
          }
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
    // Если буфер полон (pos == sizeof(buf)-1), символы молча
    // отбрасываются.
  }
  // если pos == sizeof(buf)-1, символы игнорируются
}

static void handle_discovery(int fd) {
  char ip[64], fp[65], name[64];
  uint16_t port;
  while (discovery_recv(fd, ip, fp, name, &port) == 1)
    connection_try_connect(fp, name, ip, port);
}

/*
 * ================================================================
 * ========================= DAEMON RUN ===========================
 * ================================================================
 */
int daemon_run(void) {
  log_info("Daemon starting");

  // Set commands context
  g_ctx.active = &general_chat_active;

  // sqlite3 init
  msg_store_init("chat.db");

  // SIGINT SIGTERM register handler
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  // Регистрация команд
  commands_register();

  // ——— Инициализация TLS ———
  // Загружаем сертификаты, настраиваем mTLS, вычисляем свой fingerprint
  if (transport_tls_init(g_config.ca_cert, g_config.my_cert, g_config.my_key) !=
      0) {
    log_error("TLS initialization failed");
    return 1;
  }

  // ——— Экземпляр epoll ———
  epfd = epoll_create1(0);
  if (epfd < 0) {
    log_errno("epoll_create1");
    return 1;
  }
  connection_init(epfd);

  // События epoll для наблюдения за fd
  // EPOLLIN пробуждение при появлении данных для чтения
  struct epoll_event ev = {.events = EPOLLIN};

  // ——— Инициализация Discovery (UDP Multicast) ———
  udp_fd = discovery_init(g_config.multicast_addr, g_config.multicast_port);

  if (udp_fd < 0) {
    log_error("discovery_init failed");
    close(epfd);
    return 1;
  }

  ev.data.fd = udp_fd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, udp_fd, &ev) < 0) {
    log_errno("epoll_ctl ADD udp");
    close(udp_fd);
    close(epfd);
    return 1;
  }

  // ——— TCP listen ———
  tcp_fd = transport_listen(&tcp_port);
  if (tcp_fd < 0) {
    log_error("transport_listen failed");
    close(udp_fd);
    close(epfd);
    return 1;
  }

  ev.data.fd = tcp_fd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, tcp_fd, &ev) < 0) {
    log_errno("epoll_ctl ADD tcp");
    close(tcp_fd);
    close(udp_fd);
    close(epfd);
    return 1;
  }

  log_info("TCP listening on port %u", tcp_port);

  // Неблокирующий stdin (не требуется нажатия enter)
  // Чтение флагов
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (flags == -1) {
    log_errno("fcntl F_GETFL");
    close(tcp_fd);
    close(udp_fd);
    close(epfd);
    return 1;
  }

  // Установка non block
  if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_errno("fcntl O_NONBLOCK failed");
    close(tcp_fd);
    close(udp_fd);
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
    close(tcp_fd);
    close(udp_fd);
    close(epfd);
    return 1;
  }

  // ——— Первый HELLO ———

  sleep_ms(500);
  discovery_send_hello(udp_fd, g_config.my_fp, g_config.my_name,
                       g_config.multicast_addr, g_config.multicast_port,
                       tcp_port);

  // HELLO timer
  // evs — массив, куда epoll_wait запишет сработавшие события.
  // buf — буфер для накопления ввода до '\n'.
  // pos — текущая позиция записи в buf.
  struct epoll_event evs[MAX_EVENTS];
  time_t next_hello = time(NULL) + g_config.hello_interval;

  log_debug("Entering main loop");
  // Print init msg - Help
  cli_show_help();
  // Приглашение и сброс буфера
  printf("> ");
  fflush(stdout);

  // —————————————————
  // ——— Main loop ———
  // —————————————————
  while (running) {
    // HELLO Timeout - для пробуждения epoll_wait
    int timeout_ms = (int)(next_hello - time(NULL)) * 1000;
    if (timeout_ms < 0)
      timeout_ms = 0;

    // epoll_wait блокируется до события или сигнала.
    // evs — куда записать события, 1 — размер массива событий
    // timeout_ms - ожиданиие пробуждения
    // Возвращает количество готовых fd или -1 при ошибке.
    int n = epoll_wait(epfd, evs, MAX_EVENTS, timeout_ms);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_errno("epoll_wait failed");
      break;
    }

    for (int i = 0; i < n; ++i) {
      int fd = evs[i].data.fd;

      // ***** Обработка stdin *****
      if (fd == STDIN_FILENO)
        handle_stdin();
      // ***** UDP discovery *****
      else if (fd == udp_fd)
        handle_discovery(fd);
      // ***** TCP incomming *****
      else if (fd == tcp_fd) // Обарботка данных пира
        connection_handle_incoming(fd);
      else
        connection_handle_peer_data(fd, evs[i].events);
    }

    // Периодический HELLO
    if (time(NULL) >= next_hello) {
      discovery_send_hello(udp_fd, g_config.my_fp, g_config.my_name,
                           g_config.multicast_addr, g_config.multicast_port,
                           tcp_port);
      next_hello = time(NULL) + g_config.hello_interval;
      // printf("> ");
      // fflush(stdout);
    }
  }

  // Завершение
  close(udp_fd);
  close(tcp_fd);
  close(epfd);
  log_info("Daemon stopped");

  msg_store_close();
  return 0;
}
