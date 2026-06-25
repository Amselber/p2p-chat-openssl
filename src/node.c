#include "node.h"
#include <string.h>

// Внутренний массив нод и счётчик
static node_t nodes[MAX_NODES];
static int ncount = 0;

// Добавление ноды
node_t *node_add(const char *fp, const char *name, int fd) {
  // 1. Проверяем: может, нода с таким fp уже есть?
  for (int i = 0; i < ncount; ++i) {
    if (!strcmp(nodes[i].fp, fp)) {
      // Обновляем fd (мог переподключиться)
      nodes[i].fd = fd;
      if (name && name[0]) {
        strncpy(nodes[i].name, name, 63);
        nodes[i].name[63] = '\0';
      }
      return &nodes[i];
    }
  }

  // 2. Места нет — отказ
  if (ncount >= MAX_NODES)
    return NULL;

  // 3. Занимаем новый слот
  node_t *n = &nodes[ncount++];
  memset(n, 0, sizeof(*n));
  strncpy(n->fp, fp, 64);
  strncpy(n->name, name, 63);
  n->fp[64] = '\0';   // гарантируем null-терминатор
  n->name[63] = '\0'; // гарантируем null-терминатор
  n->fd = fd;
  return n;
}

// Поиск ноды по дескриптору
node_t *node_find_by_fd(int fd) {
  for (int i = 0; i < ncount; i++) {
    if (nodes[i].fd == fd) {
      return &nodes[i];
    }
  }
  return NULL;
}

// Обход всех нод. Вызываем callback fn для каждой ноды
void node_each(void (*fn)(node_t *n, void *arg), void *arg) {
  for (int i = 0; i < ncount; i++) {
    fn(&nodes[i], arg);
  }
}

// Cброс реестра
void node_reset(void) {
  ncount = 0;
  memset(nodes, 0, sizeof(nodes));
}
