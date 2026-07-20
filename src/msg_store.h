#ifndef MSG_STORE_H
#define MSG_STORE_H

#include <time.h>

typedef struct {
  int id;
  char sender[65];
  char receiver[65];
  char text[4096];
  int edited;
  time_t timestamp;
} stored_msg_t;

/* Инициализация / завершение */
int msg_store_init(const char *path);
void msg_store_close(void);

/* Сохранить сообщение */
void msg_store_add(const char *sender, const char *receiver, const char *text);

/* Редактировать сообщение */
void msg_store_edit(int id, const char *new_text);

/* ID последнего сообщения от отправителя */
int msg_store_last_id(const char *sender);

/* Количество сообщений */
int msg_store_count(void);

/* Обход всех сообщений */
typedef void (*msg_store_cb)(stored_msg_t *msg, void *arg);
void msg_store_for_each(msg_store_cb fn, void *arg);

#endif
