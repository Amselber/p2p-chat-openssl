#include "msg_store.h"
#include "log.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *g_db = NULL;

int msg_store_init(const char *path) {
  int rc = sqlite3_open(path, &g_db);
  if (rc != SQLITE_OK) {
    log_error("msg_store: cannot open %s: %s", path, sqlite3_errmsg(g_db));
    sqlite3_close(g_db);
    g_db = NULL;
    return -1;
  }

  const char *sql = "CREATE TABLE IF NOT EXISTS messages ("
                    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  sender    TEXT    NOT NULL,"
                    "  receiver  TEXT    DEFAULT '',"
                    "  text      TEXT    NOT NULL,"
                    "  edited    INTEGER DEFAULT 0,"
                    "  timestamp INTEGER DEFAULT (strftime('%s','now'))"
                    ");";

  char *err = NULL;
  rc = sqlite3_exec(g_db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK) {
    log_error("msg_store: create table: %s", err);
    sqlite3_free(err);
    sqlite3_close(g_db);
    g_db = NULL;
    return -1;
  }

  log_info("msg_store: opened %s", path);
  return 0;
}

void msg_store_close(void) {
  if (g_db) {
    sqlite3_close(g_db);
    g_db = NULL;
  }
}

void msg_store_add(const char *sender, const char *receiver, const char *text) {
  if (!g_db)
    return;

  const char *sql =
      "INSERT INTO messages (sender, receiver, text) VALUES (?, ?, ?);";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return;

  sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, text, -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void msg_store_edit(int id, const char *new_text) {
  if (!g_db)
    return;

  const char *sql = "UPDATE messages SET text = ?, edited = 1 WHERE id = ?;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return;

  sqlite3_bind_text(stmt, 1, new_text, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int msg_store_last_id(const char *sender) {
  if (!g_db)
    return -1;

  const char *sql =
      "SELECT id FROM messages WHERE sender = ? ORDER BY id DESC LIMIT 1;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);

  int id = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    id = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  return id;
}

int msg_store_count(void) {
  if (!g_db)
    return 0;

  const char *sql = "SELECT COUNT(*) FROM messages;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return 0;

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  return count;
}

void msg_store_for_each(msg_store_cb fn, void *arg) {
  if (!g_db)
    return;

  const char *sql = "SELECT id, sender, receiver, text, edited, timestamp "
                    "FROM messages ORDER BY id;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    stored_msg_t msg;

    msg.id = sqlite3_column_int(stmt, 0);
    msg.timestamp = (time_t)sqlite3_column_int64(stmt, 5);

    const char *sender = (const char *)sqlite3_column_text(stmt, 1);
    const char *receiver = (const char *)sqlite3_column_text(stmt, 2);
    const char *text = (const char *)sqlite3_column_text(stmt, 3);
    msg.edited = sqlite3_column_int(stmt, 4);

    strncpy(msg.sender, sender ? sender : "", 64);
    strncpy(msg.receiver, receiver ? receiver : "", 64);
    strncpy(msg.text, text ? text : "", 4095);

    msg.sender[64] = '\0';
    msg.receiver[64] = '\0';
    msg.text[4095] = '\0';

    fn(&msg, arg);
  }

  sqlite3_finalize(stmt);
}
