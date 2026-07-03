// src/main.c
#include "config.h"
#include "daemon.h"
#include "log.h"
#include "version.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  const char *config_path = "p2pchat.ini";

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-c") && i + 1 < argc)
      config_path = argv[++i];
    else if (!strcmp(argv[i], "help")) {
      printf("Usage: p2pchat [-c <config>] [help|version]\n");
      return 0;
    } else if (!strcmp(argv[i], "version")) {
      printf("Version: %s\n", VERSION_STRING);
      return 0;
    }
  }

  config_load(config_path);
  strcpy(g_config.log_level, "debug");
  g_config.log_to_console = 1;
  g_config.log_file[0] = '\0';

  log_init();
  log_info("==== P2P Chat starting ====");
  log_debug("Multicast: %s:%u", g_config.multicast_addr,
            g_config.multicast_port);

  return daemon_run();
}
