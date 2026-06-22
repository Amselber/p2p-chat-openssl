// tests/test_main.c
#include "unity.h"

extern int main_test_config(void);
extern int main_test_log(void);
extern void setUp_log(void);

// Эта функция вызывается перед КАЖДЫМ тестом
void setUp(void) { setUp_log(); }

// Эта функция вызывается после КАЖДОГО теста
void tearDown(void) {}

// Точка входа для тестового модуля
int main(void) {
  int failures = 0;

  failures += main_test_config();
  failures += main_test_log();

  return failures > 0 ? 1 : 0;
}
