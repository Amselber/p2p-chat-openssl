#ifndef DISCOVERY_H
#define DISCOVERY_H
#include <stdint.h>

/*
 * discovery.h — обнаружение узлов через UDP Multicast
 *
 * Отправляет HELLO-пакеты с fingerprint и TCP-портом.
 * Принимает HELLO от других узлов.
 *
 * Формат пакета: {"fp":"<64 hex>","p":<port>}
 */

int discovery_init(const char *mcast_addr, uint16_t port);
void discovery_send_hello(int fd, const char *fp, const char *name,
                          const char *mcast_addr, uint16_t mcast_port,
                          uint16_t tcp_port);
int discovery_recv(int fd, char *ip, char *fp, char *name, uint16_t *port);

#endif
