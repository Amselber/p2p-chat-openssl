// src/discovery.c
#define _GNU_SOURCE

#include "discovery.h"
#include "log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Инициализация discovery-сокета.
 *
 * addr — IPv4-адрес multicast-группы ("239.255.0.1")
 * port — UDP-порт для приёма и отправки
 *
 * Возвращает файловый дескриптор сокета или -1 при ошибке.
 */
int discovery_init(const char *addr, uint16_t port) {
  /*
   * 1: создаём UDP-сокет.
   *
   * AF_INET  — IPv4
   * SOCK_DGRAM — датаграммный сокет (UDP, не TCP)
   * 0 — протокол по умолчанию (UDP)
   */
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    log_errno("discovery: socket");
    return -1;
  }

  /*
   * 2: разрешаем переиспользование адреса.
   *
   * SO_REUSEADDR позволяет нескольким процессам на одной машине
   * слушать один и тот же порт. Без этого флага второй экземпляр
   * не сможет сделать bind.
   */
  int reuse = 1;

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    log_errno("discovery: SO_REUSEADDR");
    // Не фатально
  }

  /*
   * 3: привязываем сокет к порту.
   *
   * bind говорит ОС: «все пакеты, пришедшие на этот порт,
   * передавай этому сокету».
   *
   * INADDR_ANY — слушаем на всех сетевых интерфейсах (lo, eth0, wlan0...)
   */
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;                // IPv4
  bind_addr.sin_port = htons(port);              // host to network short
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0

  if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    log_errno("discovery: bind");
    close(fd);
    return -1;
  }

  /*
   * 4: подписываемся на multicast-группу.
   *
   * struct ip_mreq содержит два поля:
   *   imr_multiaddr — адрес multicast-группы (ЧТО слушать)
   *   imr_interface — IP интерфейса (ГДЕ слушать, INADDR_ANY = везде)
   *
   * IP_ADD_MEMBERSHIP — команда: «добавить этот сокет в группу»
   * IPPROTO_IP — уровень протокола (IP)
   */
  struct ip_mreq mreq;
  memset(&mreq, 0, sizeof(mreq));

  // Преобразуем строковый адрес ("239.255.0.1") в бинарный
  // задаём в mreq.imr_multiaddr
  if (inet_pton(AF_INET, addr, &mreq.imr_multiaddr) != 1) {
    log_error("discovery: bad multicast address '%s'", addr);
    close(fd);
    return -1;
  }

  // INADDR_ANY = слушать на всех интерфейсах
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);

  if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    log_errno("discovery: IP_ADD_MEMBERSHIP");
    close(fd);
    return -1;
  }

  log_info("Discovery started on %s:%u", addr, port);
  return fd;
}

/*
 * Отправка HELLO-пакета в multicast-группу.
 *
 * fd         — сокет от discovery_init
 * fp         — SHA256-отпечаток своего сертификата (64 символа)
 * name       — имя узла (из CN сертификата)
 * mcast_addr — адрес multicast-группы (например "239.255.0.1")
 * mcast_port — порт multicast-группы (например 9000)
 * tcp_port   — порт, на котором узел ждёт TCP-подключений
 *
 * Пакет уходит в multicast-группу и доставляется ВСЕМ подписанным узлам.
 */
void discovery_send_hello(int fd, const char *fp, const char *name,
                          const char *mcast_addr, uint16_t mcast_port,
                          uint16_t tcp_port) {
  /*
   * 1: формируем JSON-строку.
   *
   * Пример: {"fp":"a1b2...","p":12345,"n":"alice"}
   *
   * Размер: ~150 байт — укладываемся в один UDP-пакет (MTU обычно 1500).
   */
  char buf[384];
  int len = snprintf(buf, sizeof(buf), "{\"fp\":\"%s\",\"p\":%u,\"n\":\"%s\"}",
                     fp, tcp_port, name);

  // snprintf не поместился
  if (len < 0 || len >= (int)sizeof(buf)) {
    log_error("discovery: HELLO too long (%d bytes)", len);
  }

  /*
   * 2: Адрес назначение — multicast-группа
   */
  struct sockaddr_in dest;
  memset(&dest, 0, sizeof(dest));
  dest.sin_family = AF_INET;
  dest.sin_port = htons(mcast_port);

  // inet_pton преобразует "239.255.0.1" в бинарный IP
  if (inet_pton(AF_INET, mcast_addr, &dest.sin_addr) != 1) {
    log_error("discovery: bad mcast address in send_hello");
    return;
  }

  /*
   * Шаг 3: отправляем.
   *
   * sendto отправляет датаграмму конкретному адресату (в отличие от send,
   * который требует предварительный connect).
   *
   * Параметры:
   *   fd      — сокет
   *   buf     — данные
   *   strlen  — длина данных (без '\0')
   *   0       — флаги (нет)
   *   &dest   — адрес назначения
   *   sizeof  — размер структуры адреса
   */
  ssize_t sent =
      sendto(fd, buf, strlen(buf), 0, (struct sockaddr *)&dest, sizeof(dest));

  if (sent < 0) {
    log_errno("discovery: sendto failed");
  } else {
    log_debug("HELLO sent: %zd bytes to %s:%u (fp=%s, name=%s, tcp=%u)", sent,
              mcast_addr, mcast_port, fp, name, tcp_port);
  }
}

/*
 * Приём одного HELLO-пакета (неблокирующий).
 *
 * fd       — сокет от discovery_init
 * ip_out   — буфер для IP-адреса отправителя (минимум INET6_ADDRSTRLEN = 46)
 * fp_out   — буфер для fingerprint (минимум 65 байт)
 * name_out — буфер для имени (минимум 64 байта)
 * port_out — указатель на TCP-порт отправителя
 *
 * Возвращает:
 *   1 — пакет успешно разобран
 *   0 — нет данных
 *  -1 — ошибка сокета
 */
int discovery_recv(int fd, char *ip_out, char *fp_out, char *name_out,
                   uint16_t *port_out) {
  /*
   * 1: принимаем датаграмму.
   *
   * recvfrom ждёт данные от любого отправителя.
   *
   * MSG_DONTWAIT — неблокирующий режим:
   *   если данных нет, возвращает -1 с errno = EAGAIN/EWOULDBLOCK.
   *
   * &sender — сюда запишется адрес отправителя.
   * &slen   — размер структуры sender (до вызова = sizeof, после = реальный).
   */
  char buf[512];
  struct sockaddr_in sender;
  socklen_t slen = sizeof(sender);
  ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                       (struct sockaddr *)&sender, &slen);
  // Проверка результата
  if (n < 0) {
    // EAGAIN = нет данных (для неблокирующего сокета)
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0; // нет данных
    }
    log_errno("discovery: recvfrom");
    return -1; // ошибка
  }
  // Завершаем строку
  buf[n] = '\0';

  /*
   * 3: извлекаем IP отправителя.
   *
   * inet_ntop преобразует бинарный IP в читаемую строку "192.168.1.5".
   */
  if (inet_ntop(AF_INET, &sender.sin_addr, ip_out, INET6_ADDRSTRLEN) == NULL) {
    log_error("discovery: inet_ntop failed");
    return 0;
  }

  /*
   * 3: парсим JSON.
   *
   * Формат: {"fp":"abc...","p":12345,"n":"alice"}
   *
   * Порядок полей не важен — strstr ищет подстроку в любом месте.
   */

  // *** fingerprint
  char *p = strstr(buf, "\"fp\":\"");
  if (!p) {
    log_debug("discovery: no fp in packet from %s", ip_out);
    return 0; // нет fp — игнорируем пакет
  }
  p += 6; // пропускаем "fp":"

  // ищем закрывающую кавычку
  char *e = strchr(p, '"');
  if (!e) {
    log_debug("discovery: malformed fp in packet");
    return 0;
  }

  size_t len = (size_t)(e - p);
  if (len > 64)
    len = 64; // Обрезаем до 64 символов
  memcpy(fp_out, p, len);
  fp_out[len] = '\0';

  // *** name
  name_out[0] = '\0'; // по умолчанию пустая строка
  p = strstr(buf, "\"n\":\"");
  if (p) {
    p += 5; // пропускаем "n":"
    e = strchr(p, '"');
    if (e) {
      len = (size_t)(e - p);
      if (len > 63)
        len = 63;
      memcpy(name_out, p, len);
      name_out[len] = '\0';
    }
  }

  // *** port
  p = strstr(buf, "\"p\":");
  // парсим число
  *port_out = p ? (uint16_t)atoi(p + 4) : 0; // p + 4 - пропускаем "p":
  return 1;                                  // Успешно
}
