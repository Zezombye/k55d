#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

int config_load(config *c, char *err, size_t err_size)
{
    memset(c, 0, sizeof *c);
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcpy(strrchr(path, '\\') + 1, "k55d.ini");
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }
    int have_color = 0;
    char line[2048];
    for (int line_no = 1; fgets(line, sizeof line, f); line_no++) {
        char *s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';')
            continue;
        char *eq = strchr(s, '=');
        if (!eq) {
            snprintf(err, err_size, "k55d.ini line %d: missing \"=\"", line_no);
            goto fail;
        }
        *eq = 0;
        char *key = trim(s), *value = trim(eq + 1);
        for (char *p = key; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        if (!strcmp(key, "color")) {
            if (*value == '#')
                value++;
            char *end;
            unsigned long rgb = strtoul(value, &end, 16);
            if (strlen(value) != 6 || *end) {
                snprintf(err, err_size, "k55d.ini line %d: color must be 6 hex digits, for example 5d6770", line_no);
                goto fail;
            }
            c->color[0] = (uint8_t)(rgb >> 16);
            c->color[1] = (uint8_t)(rgb >> 8);
            c->color[2] = (uint8_t)rgb;
            have_color = 1;
        } else if (!strcmp(key, "fn")) {
            if (!strcmp(value, "win"))
                c->fn_as_win = 1;
            else if (!strcmp(value, "none"))
                c->fn_as_win = 0;
            else {
                snprintf(err, err_size, "k55d.ini line %d: fn must be \"win\" or \"none\"", line_no);
                goto fail;
            }
        } else {
            snprintf(err, err_size, "k55d.ini line %d: unknown setting \"%s\"", line_no, key);
            goto fail;
        }
    }
    fclose(f);
    if (!have_color) {
        snprintf(err, err_size, "k55d.ini: \"color\" is missing");
        return -1;
    }
    return 0;
fail:
    fclose(f);
    return -1;
}
