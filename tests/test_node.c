#include "node.h"
#include "unity.h"
#include <string.h>

// Фиктивные fingerprint'ы
#define FP1 "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2"
#define FP2 "b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3"
#define FP3 "c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4"

void setUp_node(void) { node_reset(); }

// Тест: добавление новой ноды
void test_node_add_new(void) {
  node_t *n = node_add(FP1, 5);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_EQUAL_STRING(FP1, n->fp);
  TEST_ASSERT_EQUAL(5, n->fd);
}

// Тест: обновление существующей ноды
void test_node_add_update(void) {
  node_t *n1 = node_add(FP1, 5);
  TEST_ASSERT_NOT_NULL(n1);
  TEST_ASSERT_EQUAL(5, n1->fd);

  // Добавляем ту же ноду с новым fd
  node_t *n2 = node_add(FP1, 7);
  TEST_ASSERT_NOT_NULL(n2);
  TEST_ASSERT_EQUAL(7, n2->fd);

  // Это должен быть тот же указатель
  TEST_ASSERT_EQUAL_PTR(n1, n2);
}

// Тест: две разные ноды
void test_node_add_two_different(void) {
  node_t *a = node_add(FP1, 5);
  node_t *b = node_add(FP2, 6);

  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_NOT_EQUAL(a, b);
  TEST_ASSERT_EQUAL_STRING(FP1, a->fp);
  TEST_ASSERT_EQUAL_STRING(FP2, b->fp);
  TEST_ASSERT_EQUAL(5, a->fd);
  TEST_ASSERT_EQUAL(6, b->fd);
}

// Тест: find_by_fd находит
void test_node_find_by_fd_found(void) {
  node_add(FP1, 10);
  node_add(FP2, 20);

  node_t *n = node_find_by_fd(20);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_EQUAL_STRING(FP2, n->fp);
}

// Тест: find_by_fd не находит
void test_node_find_by_fd_not_found(void) {
  node_add(FP1, 10);

  node_t *n = node_find_by_fd(99);
  TEST_ASSERT_NULL(n);
}

// Тест: find_by_fd с -1
void test_node_find_by_fd_minus_one(void) {
  node_add(FP1, -1);

  node_t *n = node_find_by_fd(-1);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_EQUAL_STRING(FP1, n->fp);
}

// Вспомогательная: сбор fp в буфер через node_each
static void collect_fp(node_t *n, void *arg) {
  char *buf = (char *)arg;
  strcat(buf, n->fp);
  strcat(buf, ",");
}

// Тест: node_each обходит все
void test_node_each_iterates_all(void) {
  node_add(FP1, 1);
  node_add(FP2, 2);
  node_add(FP3, 3);

  char buf[256] = {0};
  node_each(collect_fp, buf);

  /* Проверяем что все три fp попали в буфер */
  TEST_ASSERT_NOT_NULL(strstr(buf, FP1));
  TEST_ASSERT_NOT_NULL(strstr(buf, FP2));
  TEST_ASSERT_NOT_NULL(strstr(buf, FP3));
}

// Тест: node_each с пустым реестром
void test_node_each_empty(void) {
  // Не добавляем ничего — node_each не должен падать

  char buf[256] = {0};
  node_each(collect_fp, buf);

  // Буфер должен остаться пустым
  TEST_ASSERT_EQUAL(0, buf[0]);
}

// Тест: fd = -1 (нода не подключена)
void test_node_fd_minus_one(void) {
  node_t *n = node_add(FP1, -1);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_EQUAL(-1, n->fd);
}

int main_test_node(void) {
  UNITY_BEGIN();

  RUN_TEST(test_node_add_new);
  RUN_TEST(test_node_add_update);
  RUN_TEST(test_node_add_two_different);
  RUN_TEST(test_node_find_by_fd_found);
  RUN_TEST(test_node_find_by_fd_not_found);
  RUN_TEST(test_node_find_by_fd_minus_one);
  RUN_TEST(test_node_each_iterates_all);
  RUN_TEST(test_node_each_empty);
  RUN_TEST(test_node_fd_minus_one);

  return UNITY_END();
}
