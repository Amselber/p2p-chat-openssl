#include "file_transfer.h"
#include "log.h"
#include "node.h"
#include "transport.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_TRANSFERS 4

/* ─── Состояние передачи файла ─── */
typedef enum { XFER_NONE, XFER_DATA } xfer_state_t;

typedef struct {
  int fd;
  xfer_state_t state;
  char fname[256];
  size_t size;
  size_t received;
  FILE *file;
} file_transfer_t;

static file_transfer_t g_xfers[MAX_TRANSFERS];
/*
 * ================================================================
 * ===================== Передача файлов ==========================
 * ================================================================
 */

static file_transfer_t *find_transfer(int fd) {
  for (int i = 0; i < MAX_TRANSFERS; i++) {
    if (g_xfers[i].fd == fd && g_xfers[i].state != XFER_NONE)
      return &g_xfers[i];
  }
  return NULL;
}

static file_transfer_t *alloc_transfer(int fd) {
  for (int i = 0; i < MAX_TRANSFERS; i++) {
    if (g_xfers[i].state == XFER_NONE) {
      memset(&g_xfers[i], 0, sizeof(g_xfers[i]));
      g_xfers[i].fd = fd;
      return &g_xfers[i];
    }
  }
  return NULL;
}

void file_transfer_cleanup(int fd) {
  file_transfer_t *xfer = find_transfer(fd);
  if (xfer) {
    if (xfer->file)
      fclose(xfer->file);
    memset(xfer, 0, sizeof(*xfer));
  }
}

void file_transfer_receive(int fd) {
  file_transfer_t *xfer = find_transfer(fd);
  char buf[8192];

  while (xfer->received < xfer->size) {
    size_t remaining = xfer->size - xfer->received;
    size_t to_read = sizeof(buf);
    if (remaining < to_read)
      to_read = (size_t)remaining;

    ssize_t n = transport_recv_raw(xfer->fd, buf, to_read);

    if (n <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      log_warn("File read error: fd=%d, n=%zd, errno=%d", xfer->fd, n, errno);
      fclose(xfer->file);
      memset(xfer, 0, sizeof(*xfer));
      return;
    }

    fwrite(buf, 1, (size_t)n, xfer->file);
    xfer->received += (size_t)n;
  }

  fclose(xfer->file);
  log_info("File received: %s (%ld bytes)", xfer->fname, xfer->received);
  printf("\r[FILE] %s received (%ld bytes)\n> ", xfer->fname, xfer->received);
  fflush(stdout);
  memset(xfer, 0, sizeof(*xfer));
}

void file_transfer_start(int fd, const char *header) {
  file_transfer_t *xfer = alloc_transfer(fd);
  if (!xfer) {
    log_warn("Too many transfers");
    return;
  }

  char fname[256];
  size_t size;
  if (sscanf(header, "FILE:%255[^:]:%lu", fname, &size) != 2) {
    log_warn("Bad header: %s", header);
    memset(xfer, 0, sizeof(*xfer));
    return;
  }

  char path[512];
  snprintf(path, sizeof(path), "downloads/%s", fname);

  xfer->file = fopen(path, "wb");
  if (!xfer->file) {
    log_warn("Cannot create: %s", path);
    memset(xfer, 0, sizeof(*xfer));
    return;
  }

  xfer->size = size;
  xfer->state = XFER_DATA;
  strncpy(xfer->fname, fname, 255);
  xfer->fname[255] = '\0';

  node_t *n = node_find_by_fd(fd);
  printf("\r[%s] Receiving: %s (%lu bytes)\n> ", n ? n->name : "?", fname,
         size);
  fflush(stdout);

  file_transfer_receive(xfer->fd);
}
