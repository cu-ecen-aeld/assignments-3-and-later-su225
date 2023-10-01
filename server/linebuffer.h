#ifndef __AESDSOCKET_ASSIGNMENT_LINEBUFFER_H
#define __AESDSOCKET_ASSIGNMENT_LINEBUFFER_H

#include <stdio.h>

/** 
 * Line buffer abstracts keeping the line in memory. This is
 * important for handling long lines. It is a stripped down
 * version of the string type in C++ - a vector of chars.
 */
typedef struct line_buffer {
  char*  line;
  ssize_t line_len;
  ssize_t line_cap;
}line_buffer_t;

void line_buffer_init(line_buffer_t *buff);
void line_buffer_clear(line_buffer_t *buff);
void line_buffer_destroy(line_buffer_t *buff);
int line_buffer_append(line_buffer_t *buff, char *c, size_t csz);
char *line_buffer_get(line_buffer_t *buff, ssize_t *len);

#endif