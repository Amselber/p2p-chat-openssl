// src/log.c
#ifndef LOG_H
#define LOG_H

#include <errno.h>
#include <string.h>

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_FATAL
} log_level_t;

int log_init(void);

#define log_debug(...)                                                         \
  _log_write(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)                                                          \
  _log_write(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...)                                                          \
  _log_write(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...)                                                         \
  _log_write(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...)                                                         \
  _log_write(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_errno(...)                                                         \
  _log_write(LOG_FATAL, __FILE__, __LINE__, __func__, "%s [errno=%d: %s]",     \
             __VA_ARGS__, errno, strerror(errno))

void _log_write(log_level_t level, const char *file, int line, const char *func,
                const char *fmt, ...) __attribute__((format(printf, 5, 6)));
#endif
