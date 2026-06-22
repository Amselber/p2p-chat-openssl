// src/log.c
#include "log.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static FILE *g_file = NULL;
static log_level_t g_level = LOG_INFO;
static int g_console = 1;

static const char *level_str[] = {"DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};

int log_init(void) {
  if (!strcmp(g_config.log_level, "debug"))
    g_level = LOG_DEBUG;
  else if (!strcmp(g_config.log_level, "warn"))
    g_level = LOG_WARN;
  else if (!strcmp(g_config.log_level, "error"))
    g_level = LOG_ERROR;
  else if (!strcmp(g_config.log_level, "fatal"))
    g_level = LOG_FATAL;
  else
    g_level = LOG_INFO;

  g_console = g_config.log_to_console;

  if (g_config.log_file[0]) {
    g_file = fopen(g_config.log_file, "a");
    if (!g_file)
      return -1;
  }
  return 0;
}

void _log_write(log_level_t level, const char *file, int line, const char *func,
                const char *fmt, ...) {
  if (level < g_level)
    return;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm *tm = localtime(&tv.tv_sec);
  char tbuf[32];
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm);

  const char *fname = strrchr(file, '/');
  fname = fname ? fname + 1 : file;

  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char linebuf[4608];
  int len =
      snprintf(linebuf, sizeof(linebuf), "[%s.%06ld] [%s] [%s:%d] [%s] %s\n",
               tbuf, tv.tv_usec, level_str[level], fname, line, func, msg);
  if (len < 0)
    return;

  if (g_console)
    fprintf(level >= LOG_WARN ? stderr : stdout, "%s", linebuf);
  if (g_file) {
    fputs(linebuf, g_file);
    fflush(g_file);
  }

  if (level == LOG_FATAL)
    abort();
}
