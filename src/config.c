// src/config.c
#include "config.h"
#include "log.h"
#include <ctype.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

config_t g_config;

/*
 * Загружает идентификационные данные (имя и fingerprint)
 * из PEM-файла сертификата.
 *
 * Вызывается один раз при старте, после чтения конфигурационного файла.
 * Результат сохраняется в глобальную структуру g_config.
 *
 * Формат сертификата: PEM (-----BEGIN CERTIFICATE----- ... -----END
 * CERTIFICATE-----) Имя берётся из поля CN (Common Name) в Subject сертификата.
 * Fingerprint — это SHA256 от DER-представления сертификата (64 hex-символа).
 *
 * Если сертификат не найден или повреждён — поля остаются пустыми,
 * программа продолжает работу (логируется предупреждение).
 */
static void load_identity_from_cert(void) {
  // Открываем файл сертификата
  FILE *f = fopen(g_config.my_cert, "r");
  if (!f) {
    log_warn("Cannot open cert: %s", g_config.my_cert);
    return;
  }

  /*
   * PEM_read_X509 — функция OpenSSL, которая:
   *   - читает текстовый PEM-файл (-----BEGIN CERTIFICATE-----)
   *   - декодирует Base64
   *   - создаёт структуру X509 в памяти
   *
   * Аргументы:
   *   f    — открытый файл
   *   NULL — не передаём пароль (сертификат без пароля)
   *   NULL — не используем колбек для пароля
   *   NULL — не передаём пользовательские данные
   *
   * Возвращает указатель на X509 или NULL при ошибке.
   */
  X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
  fclose(f);

  // Если не прочитался
  if (!cert) {
    log_warn("Cannot parse cert: %s", g_config.my_cert);
    return;
  }

  /*
   * Извлекаем имя
   *
   * X509_get_subject_name(cert) — получаем Subject сертификата
   *   (это структура X509_NAME, содержит набор полей: CN, O, C, ...)
   *
   * X509_NAME_get_text_by_NID — извлекаем значение конкретного поля по его NID
   *   (Numeric IDentifier).
   *
   * NID_commonName — числовой идентификатор поля CN.
   *
   * Аргументы:
   *   subject        — откуда извлекать (Subject сертификата)
   *   NID_commonName — какое поле (Common Name)
   *   g_config.my_name — куда записать строку
   *   64             — размер буфера (включая место под '\0')
   *
   */
  X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                            g_config.my_name, 64);
  g_config.my_name[63] = '\0'; // Завершаем строку

  /*
   *
   * Fingerprint = SHA256(сертификат в DER-кодировке).
   *
   * DER (Distinguished Encoding Rules) — бинарный формат сертификата.
   * В отличие от PEM, DER — это «сырые» байты, без Base64 и заголовков.
   *
   * Алгоритм:
   *   1. Преобразуем X509 → DER (бинарный массив байтов)
   *   2. Считаем SHA256 от этого массива (32 байта)
   *   3. Преобразуем каждый байт хеша в 2 hex-символа (64 символа)
   */

  /*
   * Буфер для DER-представления.
   * 8192 байт достаточно для любого сертификата (обычно 1-2 КБ).
   */
  unsigned char der[8192];

  /*
   * i2d_X509 — преобразует X509 в DER.
   *
   * Один вызов делает всё:
   *   - записывает DER-представление сертификата в буфер der
   *   - смещает указатель p на len байт вперёд (p = der + len)
   *   - возвращает фактическую длину len
   *
   * После вызова:
   *   der[0..len-1] содержит сертификат в бинарном формате DER
   *   len = размер в байтах
   */
  unsigned char *p = der;
  int len = i2d_X509(cert, &p);

  /*
   * i2d_X509 — преобразует X509 в DER.
   *
   * i2d_X509(cert, &p) записывает DER в буфер
   *               и смещает указатель p на конец записанных данных.
   *
   * p изначально указывает на начало буфера.
   * После вызова p указывает на первый байт после записанных данных.
   * Разница (p - der) = фактическая длина.
   */
  unsigned char hash[SHA256_DIGEST_LENGTH];

  /*
   * SHA256 — функция OpenSSL.
   * Аргументы:
   *   der  — данные
   *   len  — размер данных
   *   hash — куда записать хеш (32 байта)
   */
  SHA256(der, (size_t)len, hash);

  /*
   * Преобразуем бинарный хеш в hex-строку.
   *
   * Каждый байт хеша (0..255) → 2 символа (00..ff).
   *
   * sprintf с форматом "%02x":
   *   %x  — шестнадцатеричное число (a..f в нижнем регистре)
   *   02  — минимум 2 символа, дополняется нулём слева
   *
   * Пример:
   *   hash[0] = 0x0a → строка "0a"
   *   hash[0] = 0xff → строка "ff"
   *
   * Указатель: g_config.my_fp + i * 2
   *   i=0 → позиция 0
   *   i=1 → позиция 2
   *   ...
   *   i=31 → позиция 62
   *   После цикла: 64 символа
   */
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    sprintf(g_config.my_fp + i * 2, "%02x", hash[i]);
  g_config.my_fp[64] = '\0'; // Завершаем строку

  // Освобождаем память
  X509_free(cert);

  log_info("Identity loaded: %s (%s)", g_config.my_name, g_config.my_fp);
}

// Обрезаем пробелы в начале и конце строки символов
static char *trim(char *s) {
  char *start = s;

  // Сдвигаем начало
  while (isspace((unsigned char)*s))
    ++s;

  // Если строка пустая
  if (!*start) {
    *s = 0;
    return s;
  }

  // Сдвигаем конец
  char *end = s + strlen(s) - 1;

  while (end > start && isspace((unsigned char)*end))
    --end;
  *(end + 1) = 0;

  return s;
}

// Настройки по умолчанию
void config_set_defaults(void) {
  memset(&g_config, 0, sizeof(g_config));
  strcpy(g_config.multicast_addr, "239.255.0.1");
  g_config.multicast_port = 9000;
  g_config.hello_interval = 5;
  strcpy(g_config.ca_cert, "certs/rootCA.crt");
  strcpy(g_config.my_cert, "certs/client.crt");
  strcpy(g_config.my_key, "certs/client.key");
  strcpy(g_config.log_level, "info");
  g_config.log_to_console = 1;
  strcpy(g_config.log_file, "p2pchat.log");
}

// Загрузка конфига
int config_load(const char *path) {
  // Если файл не загрузится - то настройки по умолчанию
  config_set_defaults();

  FILE *f = fopen(path, "r");
  if (!f) {
    log_warn("Error openning file %s. Set defaults", path);
    return 0;
  }
  log_info("load: %s", path);

  char line[512], section[64] = "";
  while (fgets(line, sizeof(line), f)) {
    // Убираем \n в конце
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = 0;

    char *trimmed = trim(line);
    if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';')
      continue;

    // Выделяем section
    if (trimmed[0] == '[') {
      char *e = strchr(trimmed, ']');
      if (!e) {
        fclose(f);
        return -1;
      }
      *e = 0;
      strncpy(section, trimmed + 1, 63);
      section[63] = 0;
      continue;
    }

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = 0;

    // Ключ
    char *k = trim(trimmed);
    // Значение
    char *v = trim(eq + 1);

    // Убираем кавычки
    len = strlen(v);
    if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
      v[len - 1] = 0;
      v++;
    }

    // Заполняем глобальную переменную из выделенных ключей и значений
    if (!strcmp(section, "network")) {
      if (!strcmp(k, "multicast_addr"))
        strcpy(g_config.multicast_addr, v);
      else if (!strcmp(k, "multicast_port"))
        g_config.multicast_port = (uint16_t)atoi(v);
      else if (!strcmp(k, "hello_interval"))
        g_config.hello_interval = atoi(v);
    } else if (!strcmp(section, "security")) {
      if (!strcmp(k, "ca_cert"))
        strcpy(g_config.ca_cert, v);
      else if (!strcmp(k, "my_cert"))
        strcpy(g_config.my_cert, v);
      else if (!strcmp(k, "my_key"))
        strcpy(g_config.my_key, v);
    } else if (!strcmp(section, "logging")) {
      if (!strcmp(k, "level"))
        strcpy(g_config.log_level, v);
      else if (!strcmp(k, "console"))
        g_config.log_to_console = !strcmp(v, "yes");
      else if (!strcmp(k, "file"))
        strcpy(g_config.log_file, v);
    }
  }

  fclose(f);

  load_identity_from_cert();
  return 0;
}
