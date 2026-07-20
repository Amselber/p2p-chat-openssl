#ifndef COMMANDS_H
#define COMMANDS_H

/*
 * Регистрирует все команды чата в CLI.
 * Вызывается один раз при старте демона.
 */
void commands_register(void);

/*
 * Контекст, нужный командам для работы.
 * Заполняется перед вызовом commands_register.
 */
typedef struct {
  /* Флаг работы демона */
  volatile int *running;

  /* Флаг активности (join/leave) */
  volatile int *active;

  /* Файловые дескрипторы */
  int *udp_fd;
  int *tcp_fd;
  int epfd;
  int tcp_port;

  /* Колбеки для отправки */
  void (*broadcast)(const char *text);
  void (*send_to)(const char *name_or_fp, const char *text);

  /* Колбек для обновления UI */
  void (*printf_ui)(const char *fmt, ...);

} commands_ctx_t;

extern commands_ctx_t g_ctx;

#endif
