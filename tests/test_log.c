// test/test_log.c
#include "config.h"
#include "log.h"
#include "unity.h"
#include <errno.h>
#include <stdarg.h>
#include <string.h>

// Перехват вывода для проверки
static char g_capture[8192];
static int g_capture_pos = 0;

// Подменяем _log_write для тестов
// Оригинальная функция вызывает fprintf, мы перехватываем в буфер
#define _log_write test_log_write
void test_log_write(log_level_t level, const char *file, int line,
                    const char *func, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

void test_log_write(log_level_t level, const char *file, int line,
                    const char *func, const char *fmt, ...) {
  (void)file;
  (void)line;
  (void)func;

  static const char *level_str[] = {"DEBUG", "INFO ", "WARN ", "ERROR",
                                    "FATAL"};

  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  g_capture_pos += snprintf(g_capture + g_capture_pos, sizeof(g_capture),
                            "[%s] %s\n", level_str[level], msg);
}

void setUp_log(void) {
  g_capture[0] = 0;
  g_capture_pos = 0;
  g_config.log_to_console = 0;
}

// Тесты
// -----
void test_log_info_contains_message(void) {
  log_info("test message");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "test message"));
}

void test_log_error_contains_message(void) {
  log_error("error message");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "error message"));
}

void test_log_warn_contains_message(void) {
  log_warn("warning text");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "warning text"));
}

void test_log_debug_contains_message(void) {
  g_config.log_level[0] = '\0';
  strcpy(g_config.log_level, "debug");
  log_init();

  log_debug("debug info");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "debug info"));
}

void test_log_format_has_level_prefix(void) {
  log_info("hello");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "[INFO ]"));
}

void test_log_format_has_level_prefix_error(void) {
  log_error("fail");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "[ERROR]"));
}

void test_log_format_with_args(void) {
  log_info("value is %d", 42);
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "value is 42"));
}

void test_log_errno_contains_errno_string(void) {
  errno = ECONNREFUSED;
  log_errno("connect failed");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "connect failed"));
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "errno=111"));
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "Connection refused"));
}

void test_log_errno_contains_err_level(void) {
  errno = EACCES;
  log_error("permission denied");
  TEST_ASSERT_NOT_NULL(strstr(g_capture, "[ERROR]"));
}

// Запуск
int main_test_log(void) {
  UNITY_BEGIN();

  RUN_TEST(test_log_info_contains_message);
  RUN_TEST(test_log_error_contains_message);
  RUN_TEST(test_log_warn_contains_message);
  RUN_TEST(test_log_debug_contains_message);
  RUN_TEST(test_log_format_has_level_prefix);
  RUN_TEST(test_log_format_has_level_prefix_error);
  RUN_TEST(test_log_format_with_args);
  RUN_TEST(test_log_errno_contains_errno_string);
  RUN_TEST(test_log_errno_contains_err_level);

  return UNITY_END();
}
