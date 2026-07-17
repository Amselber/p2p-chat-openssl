// test/test_transport.c
#define _POSIX_C_SOURCE 199309L

#include "config.h"
#include "log.h"
#include "transport.h"
#include "unity.h"
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CA "certs/rootCA.crt"
#define CERT "certs/client.crt"
#define KEY "certs/client.key"

static int listen_fd = -1;
static uint16_t port;

/* Хелпер: сон в миллисекундах */
static void sleep_ms(int ms) {
  struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/* Неблокирующее TLS-рукопожатие между клиентом и сервером */
static int tls_handshake(int client, int server) {
  int flags = fcntl(client, F_GETFL, 0);
  fcntl(client, F_SETFL, flags | O_NONBLOCK);
  flags = fcntl(server, F_GETFL, 0);
  fcntl(server, F_SETFL, flags | O_NONBLOCK);

  int s_ok = 0, c_ok = 0;
  for (int i = 0; i < 10; i++) {
    if (!s_ok)
      s_ok = (transport_tls_accept(server) == 0);
    if (!c_ok)
      c_ok = (transport_tls_connect(client) == 0);
    if (s_ok && c_ok)
      return 0;
    sleep_ms(10);
  }
  return -1;
}

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

// tls_init загружает сертификаты
void test_tls_init_ok(void) {
  int rc = transport_tls_init(CA, CERT, KEY);
  TEST_ASSERT_EQUAL(0, rc);
  transport_tls_cleanup();
}

// fingerprint не пустой после init
void test_self_fingerprint(void) {
  transport_tls_init(CA, CERT, KEY);
  const char *fp = transport_self_fingerprint();
  TEST_ASSERT_NOT_NULL(fp);
  TEST_ASSERT_EQUAL(64, strlen(fp));
  printf("fingerprint: %s\n", fp);
  transport_tls_cleanup();
}

// полный цикл: connect → tls → send → recv
void test_tls_send_recv(void) {
  transport_tls_init(CA, CERT, KEY);

  int client = transport_connect("127.0.0.1", port);
  int server = transport_accept(listen_fd);
  TEST_ASSERT(client >= 0);
  TEST_ASSERT(server >= 0);
  TEST_ASSERT_EQUAL(0, tls_handshake(client, server));

  transport_send(client, "hello tls");
  TEST_ASSERT_EQUAL_STRING("hello tls", transport_recv(server));

  transport_tls_close(client);
  transport_tls_close(server);
  transport_close(client);
  transport_close(server);

  transport_tls_cleanup();
}

// fingerprint пира после TLS
void test_peer_fingerprint(void) {
  transport_tls_init(CA, CERT, KEY);

  int client = transport_connect("127.0.0.1", port);
  int server = transport_accept(listen_fd);

  tls_handshake(client, server);

  const char *fp_server = transport_peer_fingerprint(server);
  TEST_ASSERT_NOT_NULL(fp_server);
  TEST_ASSERT_EQUAL(64, strlen(fp_server));

  const char *fp_client = transport_peer_fingerprint(client);
  TEST_ASSERT_NOT_NULL(fp_client);
  TEST_ASSERT_EQUAL(64, strlen(fp_client));

  transport_tls_cleanup();
}

// тест send_raw для файлов
void test_send_recv_raw(void) {
  transport_tls_init(CA, CERT, KEY);

  int client = transport_connect("127.0.0.1", port);
  int server = transport_accept(listen_fd);

  unsigned char data[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
  TEST_ASSERT_EQUAL(0, transport_send_raw(client, data, sizeof(data)));

  unsigned char buf[16];
  ssize_t n = transport_recv_raw(server, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(sizeof(data), n);
  TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));

  transport_tls_cleanup();
}

int main_test_transport(void) {
  setUp_test_transport();
  UNITY_BEGIN();

  RUN_TEST(test_listen_assigns_port);
  RUN_TEST(test_send_recv_loopback);
  RUN_TEST(test_recv_null_on_close);
  RUN_TEST(test_multiple_messages);
  RUN_TEST(test_connect_refused);
  RUN_TEST(test_tls_init_ok);
  RUN_TEST(test_self_fingerprint);
  RUN_TEST(test_tls_send_recv);
  RUN_TEST(test_peer_fingerprint);
  RUN_TEST(test_send_recv_raw);

  if (listen_fd >= 0)
    transport_close(listen_fd);
  return UNITY_END();
}
