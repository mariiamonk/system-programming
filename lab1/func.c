#include "func.h"

char **read_lines(int fd, size_t *line_count) {
    char *buffer = NULL;
    size_t buffer_size = 0;
    size_t buffer_capacity = 0;

    char read_buffer[4096];
    int bytes_read;

    char **lines = NULL;
    size_t lines_capacity = 1000;
    size_t lines_size = 0;

    lines = malloc(lines_capacity * sizeof(char *));
    if (!lines) {
        return NULL;
    }

    
    while ((bytes_read = sys_read(fd, read_buffer, sizeof(read_buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            if (buffer_size >= buffer_capacity) {
                size_t new_capacity = buffer_capacity == 0 ? 1024 : buffer_capacity * 2;
                char *new_buffer = realloc(buffer, new_capacity);
                if (!new_buffer) {
                    free(buffer);
                    free_lines(lines, lines_size);
                    return NULL;
                }
                buffer = new_buffer;
                buffer_capacity = new_capacity;
            }

            char c = read_buffer[i];
            if (c == '\n') {
                if (buffer_size >= buffer_capacity) {
                    size_t new_capacity = buffer_capacity + 1;
                    char *new_buffer = realloc(buffer, new_capacity);
                    if (!new_buffer) {
                        free(buffer);
                        free_lines(lines, lines_size);
                        return NULL;
                    }
                    buffer = new_buffer;
                    buffer_capacity = new_capacity;
                }
                buffer[buffer_size] = '\0';

                if (lines_size >= lines_capacity) {
                    size_t new_capacity = lines_capacity * 2;
                    char **new_lines = realloc(lines, new_capacity * sizeof(char *));
                    if (!new_lines) {
                        free(buffer);
                        free_lines(lines, lines_size);
                        return NULL;
                    }
                    lines = new_lines;
                    lines_capacity = new_capacity;
                }

                lines[lines_size++] = buffer;
                buffer = NULL;
                buffer_size = 0;
                buffer_capacity = 0;
            } else {
                buffer[buffer_size++] = c;
            }
        }
    }

    if (buffer_size > 0) {
        if (buffer_size >= buffer_capacity) {
            size_t new_capacity = buffer_size + 1;
            char *new_buffer = realloc(buffer, new_capacity);
            if (!new_buffer) {
                free(buffer);
                free_lines(lines, lines_size);
                return NULL;
            }
            buffer = new_buffer;
            buffer_capacity = new_capacity;
        }
        buffer[buffer_size] = '\0';

        if (lines_size >= lines_capacity) {
            size_t new_capacity = lines_capacity + 1;
            char **new_lines = realloc(lines, new_capacity * sizeof(char *));
            if (!new_lines) {
                free(buffer);
                free_lines(lines, lines_size);
                return NULL;
            }
            lines = new_lines;
            lines_capacity = new_capacity;
        }

        lines[lines_size++] = buffer;
    } else if (buffer) {
        free(buffer);
    }

    if (bytes_read < 0) {
        free_lines(lines, lines_size);
        return NULL;
    }
    *line_count = lines_size;
    return lines;
}

void write_lines(int fd, char **lines, size_t line_count) {
    for (size_t i = 0; i < line_count; i++) {
        size_t len = strlen(lines[i]);
        ssize_t written = sys_write(fd, lines[i], len);
        if (written < 0) {
            return;
        }
        sys_write(fd, "\n", 1);
    }
}

void free_lines(char **lines, size_t line_count) {
    for (size_t i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}