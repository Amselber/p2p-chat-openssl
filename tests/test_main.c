// tests/test_main.c
#include "unity.h"

// Эта функция вызывается перед КАЖДЫМ тестом
void setUp(void) {}

// Эта функция вызывается после КАЖДОГО теста
void tearDown(void) {}

// Простой тест: проверяем, что 2+2=4
void test_addition(void) { TEST_ASSERT_EQUAL(4, 2 + 2); }

// Точка входа для тестового модуля
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_addition);
  return UNITY_END();
}
