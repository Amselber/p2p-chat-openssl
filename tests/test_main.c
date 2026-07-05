// tests/test_main.c
#include "unity.h"

extern int main_test_config(void);
extern int main_test_log(void);
extern int main_test_node(void);
extern int main_test_discovery(void);
extern int main_test_transport(void);

extern void setUp_log(void);
extern void setUp_node(void);
extern void setUp_test_discovery(void);

extern void tearDown_discovery(void);

// Эта функция вызывается перед КАЖДЫМ тестом
void setUp(void) {
  setUp_log();
  setUp_node();
  // setUp_discovery();
}

// Эта функция вызывается после КАЖДОГО теста
void tearDown(void) {
  // tearDown_discovery();
}

// Точка входа для тестового модуля
int main(void) {
  int failures = 0;

  failures += main_test_config();
  failures += main_test_log();
  failures += main_test_node();
  failures += main_test_discovery();
  failures += main_test_transport();

  return failures > 0 ? 1 : 0;
}
