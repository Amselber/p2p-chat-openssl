#ifndef NODE_H
#define NODE_H

/*
 * node.h — реестр узлов (нод) в P2P-сети
 *
 * Каждая нода идентифицируется fingerprint'ом сертификата (SHA256, 64
 * hex-символа). Хранит fp и fd соединения. fd == -1 означает "не подключена".
 *
 * Использование:
 *   node_t *n = node_add(fp, fd);     // добавить или обновить
 *   node_t *n = node_find_by_fd(fd);  // найти по fd
 *   node_each(my_callback, arg);      // обойти все ноды
 */

#define MAX_NODES 64

typedef struct {
  char fp[65];   // fingerprint: 64 символа + '\0'
  char name[64]; // Имя участника
  int fd;        // файловый дескриптор TCP-соединения, -1 если нет
} node_t;

/*
 * Добавляет ноду в реестр.
 * Если нода с таким fp уже существует — обновляет fd.
 * Если реестр заполнен — возвращает NULL.
 */
node_t *node_add(const char *fp, const char *name, int fd);

/*
 * Ищет ноду по файловому дескриптору.
 * Возвращает NULL если не найдена.
 */
node_t *node_find_by_fd(int fd);

/*
 * Вызывает fn для каждой ноды в реестре.
 * arg передаётся вторым аргументом в fn.
 */
void node_each(void (*fn)(node_t *n, void *arg), void *arg);

/*
 * Сброс реестра (для тестов)
 */
void node_reset(void);

#endif
