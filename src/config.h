#ifndef K55D_CONFIG_H
#define K55D_CONFIG_H

#include <windows.h>
#include <stdint.h>

typedef struct {
    uint8_t color[3];
    int fn_as_win;
} config;

// Reads k55d.ini next to the executable.
// Returns 0 on success. On error, returns -1 and writes the reason to err.
int config_load(config *c, char *err, size_t err_size);

#endif
