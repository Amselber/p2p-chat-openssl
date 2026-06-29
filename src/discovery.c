#include "discovery.h"
#include "log.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int discovery_init(const char *addr, uint16_t port) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    log_errno("discovery: socket");
    return -1;
  }

  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(port);

  if (inet_pton(AF_INET, addr, &a.sin_addr) != 1) {
    log_error("discovery: bad address %s", addr);
    close(fd);
    return -1;
  }

  if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
    log_errno("discovery: bind");
    close(fd);
    return -1;
  }

  log_info("Discovery started on %s:%u", addr, port);
  return fd;
}

void discovery_send_hello(int fd, const char *fp, const char *name,
                          const char *mcast_addr, uint16_t mcast_port,
                          uint16_t tcp_port) {
  char buf[384];
  snprintf(buf, sizeof(buf), "{\"fp\":\"%s\",\"p\":%u,\"n\":\"%s\"}", fp,
           tcp_port, name);

  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(mcast_port);
  a.sin_addr.s_addr = inet_addr(mcast_addr);

  sendto(fd, buf, strlen(buf), 0, (struct sockaddr *)&a, sizeof(a));
}

int discovery_recv(int fd, char *ip, char *fp, char *name, uint16_t *port) {
  char buf[512];
  struct sockaddr_in s;
  socklen_t sl = sizeof(s);
  ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT,
                       (struct sockaddr *)&s, &sl);
  if (n <= 0)
    return 0;
  buf[n] = 0;

  inet_ntop(AF_INET, &s.sin_addr, ip, INET6_ADDRSTRLEN);

  // fp
  char *p = strstr(buf, "\"fp\":\"");
  if (!p)
    return 0;
  p += 6;
  char *e = strchr(p, '"');
  if (!e)
    return 0;
  size_t len = (size_t)(e - p);
  if (len > 64)
    len = 64;
  memcpy(fp, p, len);
  fp[len] = 0;

  // name
  name[0] = 0;
  p = strstr(buf, "\"n\":\"");
  if (p) {
    p += 5;
    e = strchr(p, '"');
    if (e) {
      len = (size_t)(e - p);
      if (len > 63)
        len = 63;
      memcpy(name, p, len);
      name[len] = 0;
    }
  }

  // port
  p = strstr(buf, "\"p\":");
  *port = p ? (uint16_t)atoi(p + 4) : 0;
  return 1;
}
