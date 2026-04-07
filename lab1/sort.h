#ifndef SORT_H
#define SORT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef USE_ASM
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_ftruncate(int fd, off_t length);
#else
int64_t sys_open(const char *pathname, int flags, mode_t mode);
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_close(int fd);
int64_t sys_lseek(int fd, off_t offset, int whence);
int64_t sys_ftruncate(int fd, off_t length);
#endif

char **read_lines(int fd, size_t *line_count);
void sort_lines(char **lines, size_t line_count, int reverse);
void write_lines(int fd, char **lines, size_t line_count);
void free_lines(char **lines, size_t line_count);

#endif
