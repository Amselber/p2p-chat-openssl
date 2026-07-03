// src/transport.h
#ifndef TRANSPORT_H
#define TRANSPORT_H
#include <stdint.h>

/**
 * @file transport.h
 * @brief TCP-транспорт с отправкой/получением текстовых строк
 *
 * Предоставляет функции для установки TCP-соединений и обмена
 * текстовыми сообщениями. Каждое сообщение — это строка,
 * завершаемая символом '\n' при передаче.
 *
 * Этап 1: только TCP, без шифрования.
 *
 * Типичный сценарий использования:
 * @code
 *   // Сервер
 *   uint16_t port = 0;
 *   int listen_fd = transport_listen(&port);
 *   int client_fd = transport_accept(listen_fd);
 *   const char *msg = transport_recv(client_fd);
 *
 *   // Клиент
 *   int fd = transport_connect("192.168.1.5", port);
 *   transport_send(fd, "hello");
 * @endcode
 */

/* TCP connections */

/**
 * @brief Создаёт слушающий TCP-сокет
 * @param[out] port_out Назначенный порт (если передан 0, ОС выбирает свободный)
 * @return fd сокета, или -1 при ошибке
 */
int transport_listen(uint16_t *port);

/**
 * @brief Принимает входящее TCP-соединение
 * @param listen_fd fd слушающего сокета
 * @return fd для общения с клиентом, или -1 при ошибке
 */
int transport_accept(int listen_fd);

/**
 * @brief Устанавливает исходящее TCP-соединение
 * @param ip   IPv4-адрес (строка, "192.168.1.2")
 * @param port TCP-порт
 * @return fd соединения, или -1 при ошибке
 */
int transport_connect(const char *ip, uint16_t port);

/**
 * @brief Закрывает TCP-сокет
 * @param fd Файловый дескриптор
 */
void transport_close(int fd);

/* Messages */
// Отправка сообщения
int transport_send(int fd, const char *text);
// Получение сообщения, размер буфера 8192
const char *transport_recv(int fd); // NULL = соединение закрыто

#endif
