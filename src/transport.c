// src/transport.c
#include "transport.h"
#include "config.h"
#include "log.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ─── Константы ─── */

// максимальное количество одновременных соединений
// fd обычно небольшие (3..63), поэтому массив на 64
// элемента покрывает все случаи
#define MAX_CONN 64

/* ─── Глобальные переменные модуля ─── */

static SSL_CTX *g_ctx = NULL; // Контекст OpenSSL. Создаётся один раз
                              // при запуске. Содержит:
                              //   - список доверенных CA (rootCA.crt)
                              //   - свой сертификат (client.crt)
                              //   - свой приватный ключ (client.key)
                              // Все TLS-соединения создаются из этого ctx.

static SSL *g_ssl[MAX_CONN]; // Маппинг fd → SSL*.
                             // Индекс = номер файлового дескриптора.
                             // Пример: g_ssl[7] != NULL → fd=7 это TLS.
                             //         g_ssl[7] == NULL → fd=7 это TCP.

static char g_my_fp[65]; // Свой fingerprint.
                         // Вычисляется при tls_init из сертификата.
                         // Используется для:
                         //   - HELLO-пакетов (идентификация)
                         //   - проверки «это мой пакет?» в discovery

/* ─── Внутренние функции ─── */

/*
 * ssl_get — получить SSL* по fd
 *
 * Возвращает NULL если:
 *   - fd вне диапазона [0, MAX_CONN)
 *   - для этого fd нет TLS-соединения
 */
static SSL *ssl_get(int fd) {
  if (fd >= 0 && fd < MAX_CONN)
    return g_ssl[fd];
  return NULL;
}

/*
 * ssl_set — сохранить SSL* для fd
 */
static void ssl_set(int fd, SSL *s) {
  if (fd >= 0 && fd < MAX_CONN)
    g_ssl[fd] = s;
}

/*
 * ssl_del — удалить SSL* для fd (соединение закрыто)
 */
static void ssl_del(int fd) {
  if (fd >= 0 && fd < MAX_CONN)
    g_ssl[fd] = NULL;
}

/*
 * cert_fingerprint — SHA256 сертификата в hex-строку
 *
 * Алгоритм:
 *   1. Кодируем сертификат в DER (бинарный формат ASN.1)
 *   2. Считаем SHA256 от DER
 *   3. Каждый байт хеша → 2 hex-символа (00..ff)
 *
 * out: буфер на 65 байт (64 символа + '\0')
 */
static void cert_fingerprint(X509 *cert, char out[65]) {
  unsigned char der[8192]; // 8 КБ — достаточно для любого сертификата

  // i2d_X509(cert, NULL) возвращает нужный размер DER
  int len = i2d_X509(cert, NULL);
  if (len < 0 || len > (int)sizeof(der)) {
    // Сертификат слишком большой или ошибка
    strcpy(out, "00000000000000000000000000000000"
                "00000000000000000000000000000000");
    return;
  }

  // i2d_X509(cert, &p) записывает DER в буфер и смещает p
  unsigned char *p = der;
  i2d_X509(cert, &p);

  // Считаем SHA256 (32 байта)
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(der, (size_t)len, hash);

  // Преобразуем в hex: каждый байт → 2 символа
  // sprintf с "%02x" дополняет нулём слева: 0x0a → "0a"
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    sprintf(out + i * 2, "%02x", hash[i]);

  out[64] = '\0'; // завершающий нуль
}

/*
 * verify_callback — колбек OpenSSL для проверки сертификата пира
 *
 * Вызывается для каждого сертификата в цепочке.
 * preverify_ok = 1 — базовая проверка OpenSSL пройдена
 *               (подписан доверенным CA, срок не истёк, не отозван)
 * preverify_ok = 0 — что-то не так
 *
 * Мы логируем ошибку и возвращаем preverify_ok.
 * Возврат 0 → соединение будет разорвано.
 */
static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
  if (!preverify_ok) {
    int err = X509_STORE_CTX_get_error(ctx);
    log_warn("Certificate error: %s", X509_verify_cert_error_string(err));
  }
  return preverify_ok;
}

/* ──────────────────────────────
 * Инициализация и завершение
 * ──────────────────────────────  */

/*
 * transport_tls_init — инициализация TLS (вызывается один раз при старте)
 *
 * Параметры:
 *   ca_path   — путь к корневому сертификату (rootCA.crt)
 *   cert_path — путь к своему сертификату (client.crt)
 *   key_path  — путь к своему приватному ключу (client.key)
 *
 * Что делает:
 *   1. Инициализирует OpenSSL
 *   2. Создаёт SSL_CTX с настройками
 *   3. Загружает сертификаты
 *   4. Включает взаимную проверку (mTLS)
 *   5. Читает свой сертификат → fingerprint + имя → в g_config
 */
int transport_tls_init(const char *ca_path, const char *cert_path,
                       const char *key_path) {
  // 1: инициализация библиотеки OpenSSL
  SSL_library_init();       // регистрирует алгоритмы шифрования
  SSL_load_error_strings(); // загружает тексты ошибок для логов

  // 2: создание контекста
  // TLS_method() работает и для сервера, и для клиента
  g_ctx = SSL_CTX_new(TLS_method());
  if (!g_ctx) {
    log_error("SSL_CTX_new failed");
    return -1;
  }

  // 3: настройка взаимной проверки сертификатов (mTLS)
  // SSL_VERIFY_PEER — требовать сертификат от пира
  // SSL_VERIFY_FAIL_IF_NO_PEER_CERT — разорвать соединение,
  //   если пир не предъявил сертификат
  // verify_callback — наш колбек для дополнительных проверок
  SSL_CTX_set_verify(g_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                     verify_callback);

  // 4: загрузка корневого сертификата
  // Все сертификаты пиров должны быть подписаны этим CA
  if (!SSL_CTX_load_verify_locations(g_ctx, ca_path, NULL)) {
    log_error("Cannot load CA: %s", ca_path);
    return -1;
  }

  // 5: загрузка своего сертификата
  // Предъявляется пиру при TLS-рукопожатии
  if (!SSL_CTX_use_certificate_file(g_ctx, cert_path, SSL_FILETYPE_PEM)) {
    log_error("Cannot load cert: %s", cert_path);
    return -1;
  }

  // 6: загрузка своего приватного ключа
  // Используется для расшифровки и цифровой подписи
  if (!SSL_CTX_use_PrivateKey_file(g_ctx, key_path, SSL_FILETYPE_PEM)) {
    log_error("Cannot load key: %s", key_path);
    return -1;
  }

  // 7: проверка — ключ соответствует сертификату?
  if (!SSL_CTX_check_private_key(g_ctx)) {
    log_error("Private key does not match certificate");
    return -1;
  }

  // 8: читаем свой сертификат для извлечения fingerprint и имени
  X509 *my_cert = NULL;
  FILE *f = fopen(cert_path, "r");
  if (f) {
    // PEM_read_X509 читает сертификат в формате
    // -----BEGIN CERTIFICATE-----
    // ... base64 ...
    // -----END CERTIFICATE-----
    my_cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
  }

  if (my_cert) {
    // SHA256 → fingerprint
    cert_fingerprint(my_cert, g_my_fp);
    strncpy(g_config.my_fp, g_my_fp, 64);
    g_config.my_fp[64] = '\0';

    // Извлекаем Common Name (CN) из Subject
    // Пример: Subject: CN=alice → g_config.my_name = "alice"
    X509_NAME_get_text_by_NID(
        X509_get_subject_name(my_cert), // Subject сертификата
        NID_commonName,                 // "дай мне CN"
        g_config.my_name,               // буфер
        64                              // размер буфера
    );
    g_config.my_name[63] = '\0';

    X509_free(my_cert); // освобождаем структуру X509
    log_info("Identity: %s (%s)", g_config.my_name, g_my_fp);
  } else {
    log_error("Cannot read own certificate");
    return -1;
  }

  return 0;
}

/*
 * transport_tls_cleanup — завершение, освобождение ресурсов
 */
void transport_tls_cleanup(void) {
  // Закрываем все активные TLS-соединения
  for (int i = 0; i < MAX_CONN; i++) {
    if (g_ssl[i]) {
      SSL_shutdown(g_ssl[i]); // вежливое закрытие TLS (close_notify)
      SSL_free(g_ssl[i]);     // освобождение памяти
      g_ssl[i] = NULL;
    }
  }
  // Освобождаем контекст
  if (g_ctx) {
    SSL_CTX_free(g_ctx);
    g_ctx = NULL;
  }
}

/*
 * transport_self_fingerprint — свой fingerprint для HELLO-пакетов
 */
const char *transport_self_fingerprint(void) { return g_my_fp; }

/* ──────────────────────────────
 * TCP Соединения
 * ──────────────────────────────  */

/*
 * transport_listen — создать слушающий TCP-сокет
 *
 * port: если 0 — ОС назначит свободный порт
 *       если не 0 — использовать указанный порт
 *
 * Возвращает fd сокета.
 * Реальный порт записывается в *port.
 */
int transport_listen(uint16_t *port) {
  /*
   * Создаём TCP-сокет.
   * AF_INET = IPv4.
   * SOCK_STREAM = TCP (потоковый, с установлением соединения).
   */
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_errno("listen: socket");
    return -1;
  }

  int reuse = 1;
  // SO_REUSEADDR позволяет переиспользовать порт сразу после закрытия.
  // Без этого порт может висеть в состоянии TIME_WAIT до 2 минут.
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // Заполняем адрес для bind.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;    // IPv4
  addr.sin_port = htons(*port); // Порт 0 - автовыбор ОС
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  // htons, htonl (to short, to long) перевод в сетевой порядок байт

  // bind — привязываем сокет к адресу и порту.
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    log_errno("listen: bind");
    close(fd);
    return -1;
  }

  // listen — переводим сокет в режим прослушивания.
  // 8 — максимальная очередь входящих соединений.
  if (listen(fd, 8) < 0) {
    log_errno("listen");
    close(fd);
    return -1;
  }

  // getsockname — узнаём, какой порт нам назначила ОС.
  // Если *port был 0, ОС выбрала свободный порт.
  // Если *port был конкретным, возвращается он же.
  socklen_t len = sizeof(addr);
  getsockname(fd, (struct sockaddr *)&addr, &len);
  *port = ntohs(addr.sin_port);

  log_info("TCP listening on port %u", *port);
  return fd;
}

/*
 * transport_accept — принять входящее TCP-соединение
 *
 * Возвращает новый fd для общения с клиентом.
 * listen_fd продолжает слушать.
 */
int transport_accept(int listen_fd) {
  int fd = accept(listen_fd, NULL, NULL);
  if (fd < 0)
    log_errno("accept");
  return fd;
}

/*
 * transport_connect — установить TCP-соединение с удалённым узлом
 *
 * Блокируется до установки соединения или ошибки.
 */
int transport_connect(const char *ip, uint16_t port) {
  // создаём сокет
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_errno("connect: socket");
    return -1;
  }

  // Заполняем адрес назначения
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  // inet_pton — преобразует IP-строку ("192.168.1.5") в бинарный вид.
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    log_error("connect: bad address %s", ip);
    close(fd);
    return -1;
  }

  // Устанавливаем соединение
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    log_warn("connection refused: %s:%u", ip, port);
    close(fd);
    return -1;
  }
  log_debug("connect OK: %s:%u, fd=%d", ip, port, fd);

  return fd;
}

/*
 * transport_close — закрыть TCP-сокет
 *
 * Если это TLS-соединение, нужно сначала вызвать transport_tls_close.
 */
void transport_close(int fd) {
  if (fd >= 0)
    close(fd);
}

/* —————————————————————————————————————
                    TLS
   ————————————————————————————————————— */

/*
 * transport_tls_accept — TLS-рукопожатие (серверная сторона)
 *
 * Вызывается после transport_accept(), когда TCP-соединение установлено.
 *
 * Что происходит:
 *   1. Клиент предъявляет сертификат → проверяется через verify_callback
 *   2. Сервер предъявляет свой сертификат
 *   3. Согласовывается алгоритм шифрования
 *   4. Вырабатываются сеансовые ключи (Diffie-Hellman)
 *
 * После успешного рукопожатия все данные шифруются.
 */
int transport_tls_accept(int plain_fd) {
  SSL *ssl = ssl_get(plain_fd); // создаём SSL-объект из контекста
  if (!ssl) {
    ssl = SSL_new(g_ctx);
    if (!ssl)
      return -1;
    SSL_set_fd(ssl, plain_fd);
    ssl_set(plain_fd, ssl);
  }

  int ret = SSL_accept(ssl);

  if (ret == 1) {
    // Рукопожатие завершено
    log_debug("TLS accepted, fd=%d", plain_fd);
    return 0;
  }

  int err = SSL_get_error(ssl, ret);

  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
    // Рукопожатие ещё не завершено — нужно подождать
    // Устанавливаем errno для вызывающего кода
    errno = EAGAIN;
    return -1;
  }

  char errbuf[256];
  ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
  log_warn("SSL_accept failed: %s", errbuf);
  SSL_free(ssl);
  return -1;
}

/*
 * transport_tls_connect — TLS-рукопожатие (клиентская сторона)
 *
 * Вызывается после transport_connect().
 */
int transport_tls_connect(int plain_fd) {
  log_debug("=== TLS connect START fd=%d ===", plain_fd);
  log_debug("fd=%d valid? %s", plain_fd,
            fcntl(plain_fd, F_GETFL) >= 0 ? "yes" : "no");

  SSL *ssl = ssl_get(plain_fd);
  log_debug("ssl_get(%d) = %s", plain_fd, ssl ? "EXISTS" : "NULL");

  if (!ssl) {
    ssl = SSL_new(g_ctx);
    if (!ssl) {
      log_error("SSL_new failed");
      return -1;
    }
    SSL_set_fd(ssl, plain_fd);
    ssl_set(plain_fd, ssl);
    log_debug("SSL created and set for fd=%d", plain_fd);
  }

  if (SSL_is_init_finished(ssl)) {
    log_debug("TLS already finished for fd=%d", plain_fd);
    return 0;
  }

  log_debug("Calling SSL_connect(%d)...", plain_fd);
  ERR_clear_error();
  int ret = SSL_connect(ssl);
  int err = SSL_get_error(ssl, ret);
  unsigned long ossl_err = ERR_get_error();

  char errbuf[256] = {0};
  if (ossl_err)
    ERR_error_string_n(ossl_err, errbuf, sizeof(errbuf));

  log_debug("SSL_connect ret=%d, SSL_get_error=%d, ERR=%s, finished=%d", ret,
            err, ossl_err ? errbuf : "none", SSL_is_init_finished(ssl));

  if (ret == 1) {
    log_debug("TLS connect SUCCESS fd=%d", plain_fd);
    return 0;
  }

  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
    log_debug("TLS connect IN PROGRESS fd=%d", plain_fd);
    errno = EAGAIN;
    return -1;
  }

  log_warn("TLS connect FAILED fd=%d: %s", plain_fd,
           ossl_err ? errbuf : "unknown");

  SSL_free(ssl);
  ssl_del(plain_fd);
  return -1;
}

// проверяет, ожидает ли fd TLS-рукопожатия.
int transport_tls_pending(int fd) {
  // Если SSL* нет — TLS ещё не начинали
  // Если SSL* есть и SSL_is_init_finished — TLS завершён
  SSL *ssl = ssl_get(fd);
  if (!ssl)
    return 1;                        // TLS не начинали
  return !SSL_is_init_finished(ssl); // 1 = ещё в процессе, 0 = готов
}

/*
 * transport_tls_close — вежливое закрытие TLS-соединения
 *
 * SSL_shutdown отправляет пиру close_notify — «я закрываюсь».
 */
void transport_tls_close(int fd) {
  SSL *ssl = ssl_get(fd);
  if (ssl) {
    SSL_shutdown(ssl); // close_notify
    SSL_free(ssl);     // освобождение памяти
    ssl_del(fd);       // удаляем из маппинга
  }
}

/* ─── Информация о пире (доступна после TLS-рукопожатия) ─── */

/*
 * transport_peer_fingerprint — SHA256 сертификата пира
 *
 * Используется для сверки с fingerprint из HELLO-пакета:
 *   если HELLO утверждает "я alice, fp=abc123",
 *   а сертификат имеет fp=def456 → соединение разрывается.
 */
const char *transport_peer_fingerprint(int fd) {
  SSL *ssl = ssl_get(fd);
  if (!ssl)
    return NULL;

  // SSL_get_peer_certificate возвращает сертификат пира
  X509 *cert = SSL_get_peer_certificate(ssl);
  if (!cert)
    return NULL;

  static char fp[65];
  cert_fingerprint(cert, fp);
  X509_free(cert); // освобождаем копию
  return fp;
}

/*
 * transport_peer_name — Common Name (CN) из сертификата пира
 *
 * Используется для отображения имени в UI.
 */
const char *transport_peer_name(int fd) {
  SSL *ssl = ssl_get(fd);
  if (!ssl)
    return NULL;

  X509 *cert = SSL_get_peer_certificate(ssl);
  if (!cert)
    return NULL;

  static char name[64];
  X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, name,
                            sizeof(name));
  X509_free(cert);
  return name;
}

/* —————————————————————————————————————
         Отправка и приём сообщений
   ————————————————————————————————————— */

/*
 * transport_send — отправить текстовую строку
 *
 * Если fd — TLS-соединение: SSL_write (шифрование)
 * Если fd — обычный TCP:    write (без шифрования)
 *
 * Формат: текст + '\n'
 *
 * Цикл while нужен потому, что SSL_write/write могут
 * отправить не все байты за один вызов.
 */
int transport_send(int fd, const char *text) {
  SSL *ssl = ssl_get(fd);

  // Если SSL есть, но рукопожатие не завершено — не отправляем
  if (ssl && !SSL_is_init_finished(ssl)) {
    log_warn("TLS not ready for fd=%d", fd);
    return -1;
  }

  size_t len = strlen(text);
  size_t sent = 0;

  // Отправляем текст по частям, пока не уйдёт весь
  while (sent < len) {
    ssize_t n = ssl ? SSL_write(ssl, text + sent, (int)(len - sent))
                    : write(fd, text + sent, len - sent);

    if (n <= 0)
      return -1; // ошибка или соединение закрыто
    sent += (size_t)n;
  }

  // Отправляем '\n' как разделитель сообщений
  char nl = '\n';
  ssize_t n = ssl ? SSL_write(ssl, &nl, 1) : write(fd, &nl, 1);
  return (n == 1) ? 0 : -1;
}

/*
 * transport_recv — получить текстовую строку
 *
 * Читает по одному байту до '\n'.
 * Возвращает строку без '\n'.
 *
 * Возвращаемое значение:
 *   не-NULL — сообщение получено
 *   NULL    — соединение закрыто (read вернул 0)
 *           — или данных пока нет (EAGAIN в неблокирующем режиме)
 *
 * Статический буфер перезаписывается при каждом вызове.
 */
const char *transport_recv(int fd) {
  SSL *ssl = ssl_get(fd);
  static char buf[8192];
  int pos = 0;

  while (pos < (int)sizeof(buf) - 1) {
    ssize_t n = ssl ? SSL_read(ssl, buf + pos, 1) // читаем 1 байт
                    : read(fd, buf + pos, 1);

    if (n == 0) {
      // read вернул 0 → пир закрыл соединение (FIN)
      // Если что-то уже прочитали — возвращаем это
      return pos > 0 ? buf : NULL;
    }

    if (n < 0) {
      // Ошибка чтения
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        // Неблокирующий сокет: данных пока нет
        return pos > 0 ? buf : NULL;
      // Реальная ошибка
      return NULL;
    }

    // Прочитали байт
    if (buf[pos] == '\n')
      break; // конец сообщения
    pos++;
  }

  buf[pos] = '\0';
  return buf;
}
