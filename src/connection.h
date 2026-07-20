#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdint.h>

/* Инициализация (epoll fd) */
void connection_init(int epfd);

/* Обработка входящего TCP */
void connection_handle_incoming(int tcp_fd);

/* Обработка данных от пира */
void connection_handle_peer_data(int fd, uint32_t events);

/* Попытка подключения к обнаруженному узлу */
void connection_try_connect(const char *fp, const char *name, const char *ip,
                            uint16_t port);

void connection_tls_handshake(int fd, int is_incoming);
/* Закрыть все соединения */
void connection_close_all(void);

#endif
