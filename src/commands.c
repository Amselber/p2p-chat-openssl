#include "commands.h"
#include "cli.h"
#include "config.h"
#include "log.h"
#include "msg_store.h"
#include "node.h"
#include "transport.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

commands_ctx_t g_ctx;
extern volatile int running;

/* ─── Вспомогательные структуры для node_each ─── */

struct find_args {
  const char *fp;   // fingerprint для поиска
  const char *name; // имя для поиска
  int *found_fd;    // [выход] найденный fd или -1
};

/* ─── Коллбеки для node_each ─── */

/*
 * print_node — вывод информации об узле (для /nodes)
 */
static void print_node(node_t *n, void *arg) {
  (void)arg;
  printf("  %-10s : %u : %s [%s]\n", n->name, n->is_incoming, n->fp,
         n->fd != -1 ? "online" : "offline");
}

/*
 * find_by_fp — поиск узла по fingerprint в реестре
 */
static void find_node_by_fp(node_t *n, void *arg) {
  struct find_args *a = (struct find_args *)arg;
  if (!strcmp(n->fp, a->fp) && n->fd != -1)
    *a->found_fd = n->fd;
}

/*
 * find_by_name — поиск узла по имени в реестре
 */
static void find_node_by_name(node_t *n, void *arg) {
  struct find_args *a = (struct find_args *)arg;
  if (!strcmp(n->name, a->name) && n->fd != -1)
    *a->found_fd = n->fd;
}

static void print_history_msg(stored_msg_t *msg, void *arg) {
  (void)arg;
  char ts[32];
  strftime(ts, sizeof(ts), "%H:%M", localtime(&msg->timestamp));
  printf("  [%s] %s: %s%s\n", ts, msg->sender, msg->text,
         msg->edited ? " (edited)" : "");
}
/*
 * ================================================================
 * ========================= Команды CLI ==========================
 * ================================================================
 */

static CommandResult cmd_history(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("\rHistory:\n");
  msg_store_for_each(print_history_msg, NULL);
  return result_success(NULL);
}

static CommandResult cmd_edit(int argc, char **argv) {
  if (argc < 1)
    return result_error("Usage: /edit [id] <text>");

  int id, text_start;
  if (argv[0][0] >= '0' && argv[0][0] <= '9') {
    id = atoi(argv[0]);
    text_start = 1;
  } else {
    id = msg_store_last_id(g_config.my_name);
    text_start = 0;
  }

  if (id < 0)
    return result_error("No messages");

  static char text[4096];
  text[0] = '\0';
  for (int i = text_start; i < argc; i++) {
    if (i > text_start)
      strcat(text, " ");
    strcat(text, argv[i]);
  }

  msg_store_edit(id, text);

  /* Отправляем всем */
  char edit_msg[4280];
  snprintf(edit_msg, sizeof(edit_msg), "EDIT:%d:%s", id, text);
  g_ctx.broadcast(edit_msg);

  static char buf[64];
  snprintf(buf, sizeof(buf), "Edited #%d", id);
  return result_success(buf);
}

/*
 * cmd_quit — /quit
 * Завершает главный цикл.
 */
static CommandResult cmd_quit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_info("User requested quit");
  running = 0;
  return result_success(NULL);
}

/*
 * cmd_help — /help
 * Показывает справку по командам.
 */
static CommandResult cmd_help(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cli_show_help();
  return result_success(NULL);
}

/*
 * cmd_echo — /echo <text>
 * Повторяет введённый текст (для отладки CLI).
 */
static CommandResult cmd_echo(int argc, char **argv) {
  if (argc < 1)
    return result_error("Usage: /echo <text>");
  static char buf[256];
  buf[0] = 0;
  for (int i = 0; i < argc; i++) {
    if (i > 0)
      strcat(buf, " ");
    strcat(buf, argv[i]);
  }
  return result_success(buf);
}

/*
 * cmd_config — /config
 * Показывает текущую конфигурацию.
 */
static CommandResult cmd_config(int argc, char **argv) {
  (void)argc;
  (void)argv;
  static char buf[1024];
  snprintf(buf, sizeof(buf),
           "multicast: %s:%u\n"
           "hello: %d sec\n"
           "my_fp: %s\n"
           "my_name: %s\n"
           "ca: %s\n"
           "cert: %s",
           g_config.multicast_addr, g_config.multicast_port,
           g_config.hello_interval,
           g_config.my_fp[0] ? g_config.my_fp : "(not set)",
           g_config.my_name[0] ? g_config.my_name : "(not set)",
           g_config.ca_cert, g_config.my_cert);
  return result_success(buf);
}

/*
 * cmd_nodes — /nodes
 * Показывает список известных узлов.
 */
static CommandResult cmd_nodes(int argc, char **argv) {
  (void)argc;
  (void)argv;
  log_debug("Listing nodes");
  printf("\rNodes:\n");
  node_each(print_node, NULL);
  return result_success(NULL);
}

/*
 * cmd_msg — /msg <name-or-fp> <text>
 * Отправляет приватное сообщение узлу.
 *
 * Поиск: сначала по имени, затем по fingerprint.
 */
static CommandResult cmd_msg(int argc, char **argv) {
  if (argc < 2) {
    log_warn("/msg called with insufficient arguments");
    return result_error("Usage: /msg <fp> <text>");
  }

  // Ищем узел
  int found_fd = -1;
  struct find_args a = {argv[0], argv[0], &found_fd};

  node_each(find_node_by_name, &a);
  if (found_fd == -1)
    node_each(find_node_by_fp, &a);

  if (found_fd == -1) {
    log_warn("Node not online: %s", argv[0]);
    return result_error("Node not online");
  }

  // Собираем текст сообщения из оставшихся аргументов
  static char text[4096];
  text[0] = '\0';
  for (int i = 1; i < argc; ++i) {
    if (i > 1)
      strcat(text, " ");
    strcat(text, argv[i]);
  }

  // Отправляем
  if (transport_send(found_fd, text) < 0) {
    log_warn("Failed to send to %s", argv[0]);
    return result_error("Send failed");
  }

  log_debug("[msg → %s]: %s", argv[0], text);
  return result_success(NULL);
}

/*
 * /ls [path]
 * Показывает файлы в директории загрузок (или указанной).
 * Без аргументов — downloads/
 */
static CommandResult cmd_ls(int argc, char **argv) {
  const char *path = "downloads";
  if (argc >= 1)
    path = argv[0];

  DIR *dir = opendir(path);
  if (!dir)
    return result_error("Cannot open directory");

  printf("\r%-40s %10s\n", "Name", "Size");
  printf("%-40s %10s\n", "----------------------------------------",
         "----------");

  struct dirent *entry;
  struct stat st;
  char fullpath[512];

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue; // пропускаем . и ..

    snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
    if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
      long size = st.st_size;
      char sizestr[64];
      if (size < 1024)
        snprintf(sizestr, sizeof(sizestr), "%ld B", (long)size);
      else if (size < 1024 * 1024)
        snprintf(sizestr, sizeof(sizestr), "%ld KB", (long)size / 1024);
      else
        snprintf(sizestr, sizeof(sizestr), "%ld MB",
                 (long)size / (1024 * 1024));

      printf("  %-38s %10s\n", entry->d_name, sizestr);
    }
  }

  closedir(dir);
  return result_success(NULL);
}

/*
 * /file <name-or-fp> <path>
 * Отправляет файл указанному узлу.
 */
static CommandResult cmd_file(int argc, char **argv) {
  if (argc < 2)
    return result_error("Usage: /file <name-or-fp> <path>");

  // Ищем узел
  int found_fd = -1;
  struct find_args a = {argv[0], argv[0], &found_fd};
  node_each(find_node_by_name, &a);
  if (found_fd == -1)
    return result_error("Node not online");

  // if (transport_tls_pending(found_fd))
  //   return result_error("TLS not ready");

  /* Открываем файл */
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    log_error("Cannot open file: %s", argv[1]);
    return result_error("Cannot open file");
  }

  /* Размер файла */
  fseek(f, 0, SEEK_END);
  ssize_t size = ftell(f);
  rewind(f);

  /* Имя файла (без пути) */
  const char *fname = strrchr(argv[1], '/');
  fname = fname ? fname + 1 : argv[1];

  /* Отправляем заголовок */
  char header[512];
  snprintf(header, sizeof(header), "FILE:%s:%ld", fname, size);
  if (transport_send(found_fd, header) < 0) {
    fclose(f);
    return result_error("Send failed");
  }

  /* Отправляем данные */
  char buf[8192];
  size_t n;
  size_t sent = 0;

  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (transport_send_raw(found_fd, buf, n) < 0) {
      fclose(f);
      return result_error("Send failed");
    }
    sent += n;
  }

  fclose(f);

  /* Отправляем конец */
  transport_send(found_fd, "FILE_END");

  log_info("File sent: %s (%ld bytes) to %s", fname, sent, argv[0]);
  static char msg[256];
  snprintf(msg, sizeof(msg), "Sent %s (%ld bytes)", fname, sent);
  return result_success(msg);
}

// Регистрация всех команд
void commands_register(void) {
  static Command cmds[] = {{"quit", "Exit", cmd_quit},
                           {"help", "Show this help", cmd_help},
                           {"echo", "Echo text", cmd_echo},
                           {"config", "Show configuration", cmd_config},
                           {"msg", "Send private message", cmd_msg},
                           {"nodes", "List connected nodes", cmd_nodes},
                           {"file", "Send file to node", cmd_file},
                           {"ls", "List file in downloads idr", cmd_ls},
                           {"history", "Print messages history", cmd_history},
                           {"edit", "Edit message in history", cmd_edit},
                           {NULL, NULL, NULL}};

  int i;
  for (i = 0; cmds[i].name; ++i)
    cli_register(&cmds[i]);

  log_debug("Registered %d commands", i);
}
