// tests/test_main.c
#include "unity.h"
#include "unity_internals.h"

extern int main_test_config(void);

// Эта функция вызывается перед КАЖДЫМ тестом
void setUp(void) {}

// Эта функция вызывается после КАЖДОГО теста
void tearDown(void) {}

// Простой тест: проверяем, что 2+2=4
void test_addition(void) { TEST_ASSERT_EQUAL(4, 2 + 2); }

// Точка входа для тестового модуля
int main(void) {
  int failures = 0;
  UNITY_BEGIN();
  // RUN_TEST(test_addition);
  UNITY_END();

  failures += main_test_config();
  return failures > 0 ? 1 : 0;
}
