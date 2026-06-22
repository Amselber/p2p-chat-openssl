// src/main.c
#include "cli.h"
#include "config.h"
#include "daemon.h"
#include "log.h"
#include "version.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "help")) {
    cli_show_help();
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "version")) {
    printf("Version: %s\n", VERSION_STRING);
  }

  config_load("p2pchat.ini");
  strcpy(g_config.log_level, "debug");
  g_config.log_to_console = 1;
  g_config.log_file[0] = '\0';
  log_init();

  log_info("==== P2P Chat starting ====");
  log_debug("Multicast: %s:%u", g_config.multicast_addr,
            g_config.multicast_port);

  return daemon_run();
}
