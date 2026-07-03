#include "transport.h"
#include "log.h"
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int transport_listen(uint16_t *port) {
  /*
   * Создаём TCP-сокет.
   * AF_INET = IPv4.
   * SOCK_STREAM = TCP (потоковый, с установлением соединения).
   */
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_errno("listen: socket");
    return -1;
  }

  int reuse = 1;
  // SO_REUSEADDR позволяет переиспользовать порт сразу после закрытия.
  // Без этого порт может висеть в состоянии TIME_WAIT до 2 минут.
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // Заполняем адрес для bind.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;    // IPv4
  addr.sin_port = htons(*port); // Порт 0 - автовыбор ОС
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  // htons, htonl (to short, to long) перевод в сетевой порядок байт

  // bind — привязываем сокет к адресу и порту.
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    log_errno("listen: bind");
    close(fd);
    return -1;
  }

  // listen — переводим сокет в режим прослушивания.
  // 8 — максимальная очередь входящих соединений.
  if (listen(fd, 8) < 0) {
    log_errno("listen");
    close(fd);
    return -1;
  }

  // getsockname — узнаём, какой порт нам назначила ОС.
  // Если *port был 0, ОС выбрала свободный порт.
  // Если *port был конкретным, возвращается он же.
  socklen_t len = sizeof(addr);
  getsockname(fd, (struct sockaddr *)&addr, &len);
  *port = ntohs(addr.sin_port);

  log_info("TCP listening on port %u", *port);
  return fd;
}

int transport_accept(int listen_fd) {
  // accept — принимаем входящее соединение.
  // Возвращает новый fd для общения с подключившимся клиентом.
  // listen_fd продолжает слушать новые соединения.
  int fd = accept(listen_fd, NULL, NULL);
  if (fd < 0)
    log_errno("accept");
  return fd;
}

int transport_connect(const char *ip, uint16_t port) {
  // создаём сокет
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_errno("connect: socket");
    return -1;
  }

  // Заполняем адрес назначения
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  // inet_pton — преобразует IP-строку ("192.168.1.5") в бинарный вид.
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    log_error("connect: bad address %s", ip);
    close(fd);
    return -1;
  }

  // inet_pton — преобразует IP-строку ("192.168.1.5") в бинарный вид.
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    log_warn("connection refused: %s:%u", ip, port);
    close(fd);
    return -1;
  }

  return fd;
}

void transport_close(int fd) {
  // Закрываем TCP-сокет.
  if (fd >= 0)
    close(fd);
}

int transport_send(int fd, const char *text) {
  size_t len = strlen(text);
  size_t sent = 0;

  while (sent < len) {
    ssize_t n = write(fd, text + sent, len - sent);
    if (n <= 0)
      return -1;
    sent += (size_t)n;
  }

  char nl = '\n';
  ssize_t n = write(fd, &nl, 1);
  return (n == 1) ? 0 : -1;
}

const char *transport_recv(int fd) {
  static char buf[8192];
  int pos = 0;

  while (pos < (int)sizeof(buf) - 1) {
    ssize_t n = read(fd, buf + pos, 1);
    if (n <= 0)
      return pos > 0 ? buf : NULL;
    if (buf[pos] == '\n')
      break;
    pos++;
  }

  buf[pos] = '\0';
  return buf;
}
