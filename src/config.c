// src/config.c
#include "config.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

config_t g_config;

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
  if (!f)
    return 0;

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
  return 0;
}
