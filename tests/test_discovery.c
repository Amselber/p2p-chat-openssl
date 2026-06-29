// test_discovery.c
#define _POSIX_C_SOURCE 199309L

#include "discovery.h"
#include "log.h"
#include "unity.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ADDR "239.255.0.1"
#define PORT 9000
#define FP "abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234"
#define NAME "testnode"

struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000};
static int g_fd = -1;

void setUp_discovery(void) {
  if (g_fd < 0) {
    g_fd = discovery_init(ADDR, PORT);
  }
}

void tearDown_discovery(void) {
  if (g_fd >= 0) {
    close(g_fd);
    g_fd = -1;
  }
}

// 1. Инициализация возвращает валидный fd
void test_init_ok(void) {
  setUp_discovery();
  TEST_ASSERT(g_fd >= 0);
  tearDown_discovery();
}

// 2. Неправильный адрес — bind должен упасть
void test_init_bad_addr(void) {
  int fd = discovery_init("999.999.999.999", 9003);
  TEST_ASSERT_EQUAL(-1, fd);
  tearDown_discovery();
}

// 3. send_hello не падает
void test_send_no_crash(void) {
  setUp_discovery();
  discovery_send_hello(g_fd, FP, NAME, ADDR, PORT, 12345);
  TEST_ASSERT(1);
  tearDown_discovery();
}

// 4. recv на мусорный пакет возвращает 0
void test_recv_garbage(void) {
  setUp_discovery();
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(PORT);
  a.sin_addr.s_addr = inet_addr(ADDR);
  sendto(g_fd, "garbage", 7, 0, (struct sockaddr *)&a, sizeof(a));
  nanosleep(&ts, NULL);

  char ip[64], fp[65], name[64];
  uint16_t port;
  TEST_ASSERT_EQUAL(0, discovery_recv(g_fd, ip, fp, name, &port));
  tearDown_discovery();
}

// 5. recv без данных возвращает 0
void test_recv_empty(void) {
  setUp_discovery();
  char ip[64], fp[65], name[64];
  uint16_t port;
  TEST_ASSERT_EQUAL(0, discovery_recv(g_fd, ip, fp, name, &port));
  tearDown_discovery();
}

// 6. Полный цикл: send + recv
void test_send_recv(void) {
  setUp_discovery();
  discovery_send_hello(g_fd, FP, NAME, ADDR, PORT, 54321);
  nanosleep(&ts, NULL);

  char ip[64], fp[65], name[64];
  uint16_t port;
  TEST_ASSERT_EQUAL(1, discovery_recv(g_fd, ip, fp, name, &port));
  TEST_ASSERT_EQUAL_STRING(FP, fp);
  TEST_ASSERT_EQUAL_STRING(NAME, name);
  TEST_ASSERT_EQUAL(54321, port);
  TEST_ASSERT(strlen(ip) > 0);
  tearDown_discovery();
}

// 7. Парсинг без поля name
void test_recv_no_name(void) {
  char buf[256];
  snprintf(buf, sizeof(buf), "{\"fp\":\"%s\",\"p\":12345}", FP);

  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(9001);
  a.sin_addr.s_addr = inet_addr("239.255.0.1");
  sendto(g_fd, buf, strlen(buf), 0, (struct sockaddr *)&a, sizeof(a));
  nanosleep(&ts, NULL);

  char ip[64], fp[65], name[64];
  uint16_t port;
  TEST_ASSERT_EQUAL(1, discovery_recv(g_fd, ip, fp, name, &port));
  TEST_ASSERT_EQUAL_STRING(FP, fp);
  TEST_ASSERT_EQUAL(0, name[0]); // имя пустое
  TEST_ASSERT_EQUAL(12345, port);
}

// 8. Два сокета — отправка из одного, приём в другом
void test_two_sockets(void) {
  int b = discovery_init("239.255.0.1", 9004);
  TEST_ASSERT(b >= 0);

  discovery_send_hello(b, FP, NAME, ADDR, PORT, 9999);
  nanosleep(&ts, NULL);

  char ip[64], fp[65], name[64];
  uint16_t port;
  TEST_ASSERT_EQUAL(1, discovery_recv(g_fd, ip, fp, name, &port));
  TEST_ASSERT_EQUAL_STRING(FP, fp);
  TEST_ASSERT_EQUAL(9999, port);

  close(b);
}

int main_test_discovery(void) {
  log_init();
  UNITY_BEGIN();

  RUN_TEST(test_init_ok);
  RUN_TEST(test_init_bad_addr);
  RUN_TEST(test_send_no_crash);
  RUN_TEST(test_recv_garbage);
  RUN_TEST(test_recv_empty);
  RUN_TEST(test_send_recv);
  // RUN_TEST(test_recv_no_name);
  // RUN_TEST(test_two_sockets);

  if (g_fd >= 0)
    close(g_fd);
  return UNITY_END();
}
