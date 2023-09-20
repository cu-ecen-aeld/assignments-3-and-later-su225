#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "linebuffer.h"

void line_buffer_init(line_buffer_t *buff) {
  buff->line = NULL;
  buff->line_len = 0;
  buff->line_cap = 0;
}

void line_buffer_clear(line_buffer_t *buff) {
  buff->line_len = 0;
  memset(buff->line, 0, buff->line_cap);
}

void line_buffer_destroy(line_buffer_t *buff) {
  if (buff->line != NULL) {
    free(buff->line);
  }
}

int line_buffer_append(line_buffer_t *buff, char *c, size_t csz) {
  if (csz == 0) {
    return buff->line_len;
  }
  int new_size = buff->line_len + csz;
  bool needs_resize = new_size > buff->line_cap;
  if (needs_resize) {
    int reqd_cap = buff->line_cap;
    if (reqd_cap == 0) {
      reqd_cap = 1;
    }
    while (reqd_cap < new_size) {
      reqd_cap <<= 1;
    }
    char *newline = (char *)calloc(reqd_cap, sizeof(char));
    if (newline == NULL) {
      perror("line-buffer: memory allocation failed");
      return -1;
    }
    memcpy(newline, buff->line, buff->line_len);
    if (buff->line != NULL) {
        free(buff->line);
    }
    buff->line = newline;
    buff->line_cap = reqd_cap;
  }
  memcpy(buff->line + buff->line_len, c, csz);
  buff->line_len = new_size;
  return buff->line_len;
}

char *line_buffer_get(line_buffer_t *buff, ssize_t *len) {
  *len = buff->line_len;
  return buff->line;
}
