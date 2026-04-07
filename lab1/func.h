#ifndef FUNC_H
#define FUNC_H

#include "sort.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <bits/getopt_core.h>
#include <stddef.h>
#include <stdint.h>

char **read_lines(int fd, size_t *line_count);
void write_lines(int fd, char **lines, size_t line_count);
void free_lines(char **lines, size_t line_count);

#endif