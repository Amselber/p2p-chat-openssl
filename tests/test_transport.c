// test/test_transport.c
/*
 * Тесты для transport (этап 1: TCP без TLS)
 */

#include "config.h"
#include "log.h"
#include "transport.h"
#include "unity.h"
#include <string.h>
#include <unistd.h>

static int listen_fd = -1;
static uint16_t port;

void setUp_test_transport(void) {
  config_set_defaults();
  strcpy(g_config.log_level, "warn");
  log_init();

  if (listen_fd < 0) {
    port = 0;
    listen_fd = transport_listen(&port);
  }
}

// listen назначает порт
void test_listen_assigns_port(void) {
  TEST_ASSERT(listen_fd >= 0);
  TEST_ASSERT(port > 0);
}

// connect + accept + send + recv
void test_send_recv_loopback(void) {
  int client = transport_connect("127.0.0.1", port);
  TEST_ASSERT(client >= 0);

  int server = transport_accept(listen_fd);
  TEST_ASSERT(server >= 0);

  transport_send(client, "hello");
  const char *text = transport_recv(server);
  TEST_ASSERT_NOT_NULL(text);
  TEST_ASSERT_EQUAL_STRING("hello", text);

  transport_close(client);
  transport_close(server);
}

// recv возвращает NULL при закрытии
void test_recv_null_on_close(void) {
  int client = transport_connect("127.0.0.1", port);
  int server = transport_accept(listen_fd);

  transport_close(client);
  const char *text = transport_recv(server);
  TEST_ASSERT_NULL(text);

  transport_close(server);
}
// несколько сообщений подряд
void test_multiple_messages(void) {
  int client = transport_connect("127.0.0.1", port);
  int server = transport_accept(listen_fd);

  transport_send(client, "one");
  TEST_ASSERT_EQUAL_STRING("one", transport_recv(server));

  transport_send(client, "two");
  TEST_ASSERT_EQUAL_STRING("two", transport_recv(server));

  transport_send(client, "three");
  TEST_ASSERT_EQUAL_STRING("three", transport_recv(server));

  transport_close(client);
  transport_close(server);
}

// connect к несуществующему порту
void test_connect_refused(void) {
  // Открываем и сразу закрываем — порт гарантированно свободен
  uint16_t free_port = 0;
  int temp = transport_listen(&free_port);
  transport_close(temp);

  int fd = transport_connect("127.0.0.1", free_port);
  TEST_ASSERT_EQUAL(-1, fd);
}

int main_test_transport(void) {
  setUp_test_transport();
  UNITY_BEGIN();

  RUN_TEST(test_listen_assigns_port);
  RUN_TEST(test_send_recv_loopback);
  RUN_TEST(test_recv_null_on_close);
  RUN_TEST(test_multiple_messages);
  RUN_TEST(test_connect_refused);

  if (listen_fd >= 0)
    transport_close(listen_fd);
  return UNITY_END();
}
