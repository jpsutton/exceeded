#ifndef PNG_H
#define PNG_H
#include <stdint.h>
/* Write an 8-bit RGBA PNG (row-major, w*h*4 bytes). Returns 0 on success. */
int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba);
#endif
