// src/config.h
#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

// ! TODO: сделать g_config.my_name
typedef struct {
  char multicast_addr[64];
  uint16_t multicast_port;
  int hello_interval;

  char ca_cert[256];
  char my_cert[256];
  char my_key[256];

  char log_level[16];
  int log_to_console;
  char log_file[256];

  char my_fp[65]; // SHA256 fingerprint
  char my_name[64];
} config_t;

extern config_t g_config;

int config_load(const char *path);
void config_set_defaults(void);
#endif
