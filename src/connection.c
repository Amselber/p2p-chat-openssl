#include "connection.h"
#include "file_transfer.h"
#include "log.h"
#include "msg_store.h"
#include "node.h"
#include "transport.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>

static int g_epfd = -1;
extern int general_chat_active;

void connection_init(int epfd) { g_epfd = epfd; }

/* do_tls_handshake — выполняет TLS-рукопожатие для одного узла.
 * Вызывается из команды /tls и из /tls_all.
 */
static void connection_tls_handshake(int fd, int is_incoming) {
  // Отсутствует TCP-соединение с узлом
  if (fd <= 0)
    return; // офлайн

  // Ожидаем TLS-рукопожатие?
  if (!transport_tls_pending(fd))
    return;

  log_debug("do_tls_handshake ENTER: fd=%d, is_incoming=%d, pending=%d", fd,
            is_incoming, transport_tls_pending(fd));
  int rc = is_incoming ? transport_tls_accept(fd) : transport_tls_connect(fd);

  if (rc == 0) {
    const char *peer_fp = transport_peer_fingerprint(fd);
    const char *peer_name = transport_peer_name(fd);

    if (peer_fp) {
      log_info("TLS established with %s (%s), fd=%d",
               peer_name ? peer_name : "?", peer_fp, fd);
      node_add(peer_fp, peer_name ? peer_name : "?", fd, is_incoming);
      printf("[TLS] %s verified\n", peer_name ? peer_name : peer_fp);
      fflush(stdout);
    }
    log_debug("do_tls_handshake EXIT: fd=%d", fd);
    printf("> ");
    fflush(stdout);
    return;
  }

  log_debug("do_tls_handshake EXIT: fd=%d", fd);
  if (errno == EAGAIN) {
    printf("[TLS] : handshake in progress...\n");
    return;
  }

  printf("[TLS] : handshake failed\n");
}

/*
 * Устанавливаем соединение после получения нужных данных из HELLO
 */
void connection_try_connect(const char *fp, const char *name, const char *ip,
                            uint16_t port) {
  node_t *node = node_find(-1, fp, name);
  // Уже подключены
  if (node)
    return;

  const char *my_fp = transport_self_fingerprint();
  // Правило: меньший fp не подулючается к большему
  // Если равен 0: это мы сами - пропускаем
  // (ждёт входящее от него)
  if (my_fp && strcmp(my_fp, fp) >= 0) {
    // log_debug("I'm senior to %s, waiting for incoming", name);
    return;
  }

  int cfd = transport_connect(ip, port);
  if (cfd < 0) {
    log_warn("TCP connect failed to %s @ %s:%u", name, ip, port);
    return;
  }

  // Неблокирующий режим
  int flags = fcntl(cfd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

  // Добавляем узел в реестр
  node_add(fp, name, cfd, 0);

  // Добавляем fd в epoll
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = cfd};
  epoll_ctl(g_epfd, EPOLL_CTL_ADD, cfd, &ev);

  log_info("TCP connected to %s (%s) @ %s:%u, fd=%d", name, fp, ip, port, cfd);

  // Начинаем рукопожатие как инициатор
  connection_tls_handshake(cfd, 0);
}

/*
 * ================================================================
 * ========================= Подключение к узлу ===================
 * ================================================================
 */

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
void connection_handle_incoming(int tcp_fd) {
  // 1: принимаем TCP-соединение
  int cfd = transport_accept(tcp_fd);
  if (cfd < 0)
    return;

  // 2: неблокирующий режим
  int flags = fcntl(cfd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

  // 3: добавляем узел
  node_add(NULL, NULL, cfd, 1);

  // 6: добавляем в epoll
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = cfd};
  epoll_ctl(g_epfd, EPOLL_CTL_ADD, cfd, &ev);

  log_info("TCP accepted, fd=%d", cfd);
  printf("\r\n> ");
  fflush(stdout);
}

/*
 * handle_peer_data — обработка данных от подключённого узла
 *
 * Вызывается когда epoll сообщает, что на fd есть данные (EPOLLIN).
 * Читаем строку через transport_recv и выводим на экран.
 */
void connection_handle_peer_data(int fd, uint32_t events) {
  node_t *n = node_find_by_fd(fd);

  // Проверяем, не закрыто ли соединение
  if (events & (EPOLLHUP | EPOLLRDHUP)) {
    file_transfer_cleanup(fd);
    if (n) {
      log_info("Disconnected: %s (fd=%d)", n->name, fd);
      printf("\r[%s disconnected]\n> ", n->name);
      n->fd = -1;
    }
    epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
    transport_tls_close(fd);
    transport_close(fd);
    return;
  }

  if (!n) {
    log_warn("handle peer data: node is NULL");
    return;
  }

  // Если TLS ещё не готов — продолжаем рукопожатие
  connection_tls_handshake(fd, n->is_incoming);

  // Читаем сообщение
  const char *text = transport_recv(fd);

  if (!text)
    return;

  if (text) {
    if (strncmp(text, "FILE:", 5) == 0) {
      // Начало передачи
      file_transfer_start(fd, text);
      return;
    }

    msg_store_add(n ? n->name : "???", "", text);

    if (strncmp(text, "EDIT:", 5) == 0) {
      int id;
      char new_text[4096];
      if (sscanf(text, "EDIT:%d:%4095[^\n]", &id, new_text) == 2) {
        msg_store_edit(id, new_text);
        // показать в UI
      }
      return;
    }

    // Сообщение получено — выводим
    if (!strcmp(text, "/msg")) {
      // Приватное сообщение /msg
      printf("\r[%s (private)]: %s\n> ", n ? n->name : "???", text + 1);
      fflush(stdout);
      msg_store_add(n ? n->name : "???", "private", text + 1);
    } else {
      // Общий чат
      msg_store_add(n ? n->name : "???", "", text);
      if (general_chat_active) {
        printf("\r[%s]: %s\n> ", n ? n->name : "???", text);
        fflush(stdout);
      }
      // Если не active — сообщение сохранено, но не показано
    }
  }
}

static void _close_connection(node_t *n, void *arg) {
  (void)arg;
  if (n->fd != -1) {
    transport_tls_close(n->fd);
    transport_close(n->fd);
    n->fd = -1;
  }
}

void connection_close_all(void) { node_each(_close_connection, NULL); }
