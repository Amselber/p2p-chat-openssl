// tests/test_config.c
#include "config.h"
#include "unity.h"
#include <unistd.h>

#define TEST_CONFIG "tests/test_config.ini"

// Проверка рабочей директории (отключено в main)
void test_print_working_directory(void) {
  char cwd[512];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("\n>>> Current directory: %s\n", cwd);
  }

  // Проверим существование файла по разным путям
  const char *paths[] = {
      "tests/test_config.ini",
      "src/tests/test_config.ini",
      "test_config.ini",
      "../tests/test_config.ini",
  };

  for (int i = 0; i < 4; i++) {
    FILE *f = fopen(paths[i], "r");
    printf("    %s → %s\n", paths[i], f ? "FOUND" : "not found");
    if (f)
      fclose(f);
  }

  TEST_ASSERT_TRUE(1); // просто чтобы тест прошёл
}

// Конфигурация по умолчанию
void test_defaults(void) {
  config_set_defaults();
  TEST_ASSERT_EQUAL_STRING("239.255.0.1", g_config.multicast_addr);
  TEST_ASSERT_EQUAL_UINT16(9000, g_config.multicast_port);
  TEST_ASSERT_EQUAL_INT(5, g_config.hello_interval);
  TEST_ASSERT_EQUAL_STRING("certs/rootCA.crt", g_config.ca_cert);
  TEST_ASSERT_EQUAL_STRING("info", g_config.log_level);
  TEST_ASSERT_EQUAL_INT(1, g_config.log_to_console);
}

// Загрузка из файла
void test_load_file(void) {
  int rc = config_load(TEST_CONFIG);

  TEST_ASSERT_EQUAL_INT(0, rc);
  TEST_ASSERT_EQUAL_STRING("224.0.0.1", g_config.multicast_addr);
  TEST_ASSERT_EQUAL_UINT16(8000, g_config.multicast_port);
  TEST_ASSERT_EQUAL_INT(10, g_config.hello_interval);
  TEST_ASSERT_EQUAL_STRING("/etc/certs/ca.crt", g_config.ca_cert);
  TEST_ASSERT_EQUAL_STRING("debug", g_config.log_level);
  TEST_ASSERT_EQUAL_INT(0, g_config.log_to_console);
}

// Нет файла — не ошибка
void test_missing_file(void) {
  int rc = config_load("/tmp/nonexistent_p2pchat.conf");
  TEST_ASSERT_EQUAL_INT(0, rc);
}

// Главная функция тестового модуля
int main_test_config(void) {
  UNITY_BEGIN();
  // RUN_TEST(test_print_working_directory);
  RUN_TEST(test_defaults);
  RUN_TEST(test_load_file);
  RUN_TEST(test_missing_file);
  return UNITY_END();
}
