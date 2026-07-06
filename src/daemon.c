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

#include "daemon.h"
#include "cli.h"
#include "config.h"
#include "discovery.h"
#include "log.h"
#include "node.h"
#include "transport.h"
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 16

/* ─── Глобальные переменные ─── */
static volatile int running = 1;
static int epfd;
static int udp_fd;
static int tcp_fd;
static uint16_t tcp_port = 0; // 0 - OS Selected

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

/* ─── Вспомогательные структуры для node_each ─── */

struct broadcast_args {
  const char *text; // текст для отправки
};

struct find_args {
  const char *fp;   // fingerprint для поиска
  const char *name; // имя для поиска
  int *found_fd;    // [выход] найденный fd или -1
};

/* ─── Коллбеки для node_each ─── */

/*
 * print_node — вывод информации об узле (для /nodes)
 */
static void print_node(node_t *n, void *arg) {
  (void)arg;
  printf("  %-20s  %s [%s]\n", n->name, n->fp,
         n->fd != -1 ? "online" : "offline");
}

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

/*
 * find_by_fp — поиск узла по fingerprint в реестре
 */
static void find_node_by_fp(node_t *n, void *arg) {
  struct find_args *a = (struct find_args *)arg;
  if (!strcmp(n->fp, a->fp) && n->fd != -1)
    *a->found_fd = n->fd;
}

/*
 * find_by_name — поиск узла по имени в реестре
 */
static void find_node_by_name(node_t *n, void *arg) {
  struct find_args *a = (struct find_args *)arg;
  if (!strcmp(n->name, a->name) && n->fd != -1)
    *a->found_fd = n->fd;
}

/*
 * do_tls_handshake — выполняет TLS-рукопожатие для одного узла.
 * Вызывается из команды /tls и из /tls_all.
 */
static void do_tls_handshake(int fd) {
  if (fd == -1)
    return; // офлайн
  if (!transport_tls_pending(fd))
    return; // TLS уже готов

  int rc = transport_tls_connect(fd);

  if (rc == 0) {
    const char *peer_fp = transport_peer_fingerprint(fd);
    const char *peer_name = transport_peer_name(fd);

    if (peer_fp) {
      log_info("TLS established with %s (%s), fd=%d",
               peer_name ? peer_name : "?", peer_fp, fd);
      printf("[TLS] %s verified\n", peer_name ? peer_name : peer_fp);
    }
    return;
  }

  if (errno == EAGAIN) {
    printf("[TLS] : handshake in progress...\n");
    return;
  }

  printf("[TLS] : handshake failed\n");
}

static void accept_tls_handshake(int fd) {
  if (fd == -1)
    return; // офлайн
  if (!transport_tls_pending(fd))
    return; // TLS уже готов

  int rc = transport_tls_accept(fd);

  if (rc == 0) {
    const char *peer_fp = transport_peer_fingerprint(fd);
    const char *peer_name = transport_peer_name(fd);

    if (peer_fp) {
      log_info("TLS established with %s (%s), fd=%d",
               peer_name ? peer_name : "?", peer_fp, fd);
      printf("[TLS] %s verified\n", peer_name ? peer_name : peer_fp);
    }
    return;
  }

  if (errno == EAGAIN) {
    printf("[TLS] : handshake in progress...\n");
    return;
  }

  printf("[TLS] : handshake failed\n");
}
/*
 * ================================================================
 * ========================= Команды CLI ==========================
 * ================================================================
 */

/*
 * cmd_quit — /quit
 * Завершает главный цикл.
 */
static CommandResult cmd_quit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_info("User requested quit");
  running = 0;
  return result_success(NULL);
}

/*
 * cmd_help — /help
 * Показывает справку по командам.
 */
static CommandResult cmd_help(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_show_help();
  return result_success(NULL);
}

/*
 * cmd_echo — /echo <text>
 * Повторяет введённый текст (для отладки CLI).
 */
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

/*
 * cmd_config — /config
 * Показывает текущую конфигурацию.
 */
static CommandResult cmd_config(int argc, char **argv) {
  (void)argc;
  (void)argv;
  static char buf[1024];
  snprintf(buf, sizeof(buf),
           "multicast: %s:%u\n"
           "hello: %d sec\n"
           "my_fp: %s\n"
           "my_name: %s\n"
           "ca: %s\n"
           "cert: %s",
           g_config.multicast_addr, g_config.multicast_port,
           g_config.hello_interval,
           g_config.my_fp[0] ? g_config.my_fp : "(not set)",
           g_config.my_name[0] ? g_config.my_name : "(not set)",
           g_config.ca_cert, g_config.my_cert);
  return result_success(buf);
}

/*
 * cmd_nodes — /nodes
 * Показывает список известных узлов.
 */
static CommandResult cmd_nodes(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_debug("Listing nodes");
  printf("\rNodes:\n");
  node_each(print_node, NULL);
  return result_success(NULL);
}

/*
 * cmd_msg — /msg <name-or-fp> <text>
 * Отправляет приватное сообщение узлу.
 *
 * Поиск: сначала по имени, затем по fingerprint.
 */
static CommandResult cmd_msg(int argc, char **argv) {
  if (argc < 2) {
    log_warn("/msg called with insufficient arguments");
    return result_error("Usage: /msg <fp> <text>");
  }

  // Ищем узел
  int found_fd = -1;
  struct find_args a = {argv[0], argv[0], &found_fd};

  node_each(find_node_by_name, &a);
  if (found_fd == -1)
    node_each(find_node_by_fp, &a);

  if (found_fd == -1) {
    log_warn("Node not online: %s", argv[0]);
    return result_error("Node not online");
  }

  // Собираем текст сообщения из оставшихся аргументов
  static char text[4096];
  text[0] = '\0';
  for (int i = 1; i < argc; ++i) {
    if (i > 1)
      strcat(text, " ");
    strcat(text, argv[i]);
  }

  // Отправляем
  if (transport_send(found_fd, text) < 0) {
    log_warn("Failed to send to %s", argv[0]);
    return result_error("Send failed");
  }

  log_debug("[msg → %s]: %s", argv[0], text);
  return result_success(NULL);
}

static CommandResult cmd_send_hello(int argc, char **argv) {
  (void)argc;
  (void)argv;
  discovery_send_hello(udp_fd, g_config.my_fp, g_config.my_name,
                       g_config.multicast_addr, g_config.multicast_port,
                       tcp_port);
  return result_success(NULL);
}

/*
 * /tls <name-or-fp>
 * Запускает TLS-рукопожатие с указанным узлом.
 */
static CommandResult cmd_tls(int argc, char **argv) {
  if (argc < 1) {
    log_warn("/_tls called with insufficient arguments");
    return result_error("Usage: /_tls <name-or-fp>");
  }

  // Ищем узел
  int found_fd = -1;
  struct find_args a = {argv[0], argv[0], &found_fd};

  node_each(find_node_by_name, &a);
  if (found_fd == -1)
    node_each(find_node_by_fp, &a);

  if (found_fd == -1) {
    log_warn("Node not online: %s", argv[0]);
    return result_error("Node not online");
  }

  if (!found_fd || found_fd == -1)
    return result_error("Node not online");

  if (!transport_tls_pending(found_fd))
    return result_success("TLS already established");

  do_tls_handshake(found_fd);

  if (!transport_tls_pending(found_fd))
    return result_success("TLS established");
  else if (errno == EAGAIN)
    return result_success("TLS handshake in progress");
  else
    return result_error("TLS handshake failed");
}

// Регистрация всех команд
static void register_commands(void) {
  static Command cmds[] = {{"quit", "Exit", cmd_quit},
                           {"help", "Show this help", cmd_help},
                           {"echo", "Echo text", cmd_echo},
                           {"config", "Show configuration", cmd_config},
                           {"msg", "Send private message", cmd_msg},
                           {"nodes", "List connected nodes", cmd_nodes},
                           {"_hello", "Discovery send HELLO", cmd_send_hello},
                           {"_tls", "TLS handshake with node", cmd_tls},
                           {NULL, NULL, NULL}};

  int i;
  for (i = 0; cmds[i].name; ++i)
    cli_register(&cmds[i]);

  log_debug("Registered %d commands", i);
}

/*
 * ================================================================
 * ========================= Подключение к узлу ===================
 * ================================================================
 */

/*
 * try_connect — устанавливает TCP+TLS соединение с узлом
 *
 * Порядок:
 *   1. Проверяем, не подключены ли уже к этому узлу (по fingerprint)
 *   2. transport_connect()     — TCP-соединение
 *   3. transport_tls_connect() — TLS-рукопожатие
 *   4. Сверяем fingerprint из HELLO с сертификатом
 *   5. Добавляем узел в реестр (node_add)
 *   6. Добавляем fd в epoll
 *
 * Если любая проверка не проходит — соединение закрывается.
 */
static void try_connect(const char *fp, const char *name, const char *ip,
                        uint16_t port, int epfd) {
  // 1: Уже подключены?
  int found_fd = -1;
  struct find_args a = {fp, name, &found_fd};
  node_each(find_node_by_fp, &a);
  if (found_fd == -1) {
    // Ищем по имени
    node_each(find_node_by_name, &a);
  }
  if (found_fd != -1) {
    log_debug("Already connected to %s (fd=%d)", name, found_fd);
    return; // подключены
  }

  // Правило: меньший fp не подулючается к большему
  // (ждёт входящее от него)
  if (strcmp(g_config.my_fp, fp) > 0) {
    log_debug("I'm senior to %s, waiting for incoming", name);
    return;
  }
  log_debug("I'm junior to %s, connecting", name);

  // 2: Подключаемся по TCP
  int cfd = transport_connect(ip, port);
  if (cfd < 0) {
    log_warn("TCP connect failed to %s @ %s:%u", name, ip, port);
    return;
  }
  log_info("TCP accepted: %u, port: %u", cfd, tcp_port);

  // 3: Неблокирующий режим
  int flags = fcntl(cfd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

  // 4: Добавляем узел в реестр
  node_add(fp, name, cfd);

  // 5: Добавляем fd в epoll
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = cfd};
  epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

  log_info("TCP connected to %s (%s) @ %s:%u, fd=%d", name, fp, ip, port, cfd);
  printf("\r[%s connected]\n> ", name);
  fflush(stdout);
}

/*
 * handle_incoming — обработка входящего TCP-соединения
 *
 * Порядок:
 *   1. transport_accept()     — принимаем TCP
 *   2. transport_tls_accept() — TLS-рукопожатие (серверная сторона)
 *   3. Извлекаем fingerprint и имя из сертификата пира
 *   4. Добавляем узел в реестр
 *   5. Добавляем fd в epoll
 */
static void handle_incoming(int tcp_fd) {
  // 1: принимаем TCP-соединение
  int cfd = transport_accept(tcp_fd);
  if (cfd < 0)
    return;

  // 2: неблокирующий режим
  int flags = fcntl(cfd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

  // 3: добавляем узел
  node_add("", "incoming", cfd);

  // 6: добавляем в epoll
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = cfd};
  epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

  log_info("TCP accepted, fd=%d", cfd);
}

/*
 * handle_peer_data — обработка данных от подключённого узла
 *
 * Вызывается когда epoll сообщает, что на fd есть данные (EPOLLIN).
 * Читаем строку через transport_recv и выводим на экран.
 */
static void handle_peer_data(int fd, uint32_t events) {
  // Проверяем, не закрыто ли соединение
  if (events & (EPOLLHUP | EPOLLRDHUP)) {
    node_t *n = node_find_by_fd(fd);
    if (n) {
      log_info("Disconnected: %s (fd=%d)", n->name, fd);
      printf("\r[%s disconnected]\n> ", n->name);
      n->fd = -1;
    }
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    transport_tls_close(fd);
    transport_close(fd);
    fflush(stdout);
    return;
  }

  // ── TLS ещё не установлен? ──
  if (transport_tls_pending(fd)) {
    accept_tls_handshake(fd);
    return;
  }

  // Читаем сообщение
  const char *text = transport_recv(fd);
  if (text) {
    // Сообщение получено — выводим
    node_t *n = node_find_by_fd(fd);
    printf("\r[%s]: %s\n> ", n ? n->name : "???", text);
    fflush(stdout);
  }
  // Если text == NULL и это не EPOLLHUP — значит данных пока нет (EAGAIN).
  // Ничего не делаем, epoll сообщит когда будут.
}

/*
 * ================================================================
 * ===================== Обработчики FD ===========================
 * ================================================================
 */

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
          log_debug("Send message to all connected nodes: %s", buf);
          struct broadcast_args ba = {buf};
          node_each(send_to_node, &ba);
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

  while (discovery_recv(fd, ip, fp, name, &port) == 1) {
    // Игнорируем свои пакеты
    if (!strcmp(fp, g_config.my_fp))
      continue;

    log_info("Discovered: %s (%s) @ %s:%u", name, fp, ip, port);

    printf("\r[%s discovered %s:%u]\n> ", name, ip, port);
    fflush(stdout);

    try_connect(fp, name, ip, port, epfd);
  }
}

/*
 * ================================================================
 * ========================= DAEMON RUN ===========================
 * ================================================================
 */
int daemon_run(void) {
  log_info("Daemon starting");

  // SIGINT SIGTERM register handler
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  // Регистрация команд
  register_commands();

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

  // discovery_send_hello(udp_fd, g_config.my_fp, g_config.my_name,
  //                      g_config.multicast_addr, g_config.multicast_port,
  //                      tcp_port);

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
        handle_incoming(fd);
      else
        handle_peer_data(fd, evs[i].events);
    }

    // Периодический HELLO
    if (time(NULL) >= next_hello) {
      // discovery_send_hello(udp_fd, g_config.my_fp, g_config.my_name,
      //                      g_config.multicast_addr, g_config.multicast_port,
      //                      tcp_port);
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
  return 0;
}
