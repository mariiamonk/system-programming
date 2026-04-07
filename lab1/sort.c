#include "func.h"


#ifndef USE_ASM
int64_t sys_open(const char *pathname, int flags, mode_t mode) {
    return open(pathname, flags, mode);
}

int64_t sys_read(int fd, void *buf, size_t count) {
    return read(fd, buf, count);
}

int64_t sys_write(int fd, const void *buf, size_t count) {
    return write(fd, buf, count);
}

int64_t sys_close(int fd) {
    return close(fd);
}

#else
int64_t sys_open(const char *pathname, int flags, mode_t mode) {
    int64_t ret;
    __asm__ __volatile__ (
        "movq $2, %%rax\n"
        "movq %1, %%rdi\n"
        "movq %2, %%rsi\n"
        "movq %3, %%rdx\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r" (ret)
        : "r" (pathname), "r" ((int64_t)flags), "r" ((int64_t)mode)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_read(int fd, void *buf, size_t count) {
    int64_t ret;
    __asm__ __volatile__ (
        "movq $0, %%rax\n"
        "movq %1, %%rdi\n"
        "movq %2, %%rsi\n"
        "movq %3, %%rdx\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r" (ret)
        : "r" ((int64_t)fd), "r" (buf), "r" (count)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_write(int fd, const void *buf, size_t count) {
    int64_t ret;
    __asm__ __volatile__ (
        "movq $1, %%rax\n"
        "movq %1, %%rdi\n"
        "movq %2, %%rsi\n"
        "movq %3, %%rdx\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r" (ret)
        : "r" ((int64_t)fd), "r" (buf), "r" (count)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_close(int fd) {
    int64_t ret;
    __asm__ __volatile__ (
        "movq $3, %%rax\n"
        "movq %1, %%rdi\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r" (ret)
        : "r" ((int64_t)fd)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

#endif


int case_insensitive = 0;

const char *skip_wt(const char *str) {
        while (*str == ' ' || *str == '\t') {
            str++;
        }
        return str;
}

int str_cmp(char const *str1, char const *str2)
{
    str1 = skip_wt(str1);
    str2 = skip_wt(str2);
    char char1;
    char char2;
    
    while (*str1 != '\0' && *str2 != '\0') {
        char1 = *str1;
        char2 = *str2;

        if (case_insensitive) {
            char1 = tolower((unsigned char)char1);
            char2 = tolower((unsigned char)char2);
        }
        if (char1 != char2) {
            return (unsigned char)char1 - (unsigned char)char2;
        }
        str1++;
        str2++;
    }
    return (unsigned char)*str1 - (unsigned char)*str2;
}

int compare_strings(const void *a, const void *b) {
    return str_cmp(*(const char **)a, *(const char **)b);
}

int compare_strings_reverse(const void *a, const void *b) {
    return str_cmp(*(const char **)b, *(const char **)a);
}

void sort_lines(char **lines, size_t line_count, int reverse) {
    if (reverse) {
        qsort(lines, line_count, sizeof(char *), compare_strings_reverse);
    } else {
        qsort(lines, line_count, sizeof(char *), compare_strings);
    }
}


int process_file(char **filenames, int num_files, int reverse) {
    char **all_lines = NULL;
    size_t total_count = 0;
    int return_code = 0;
    
    if (num_files == 0) {
        all_lines = read_lines(0, &total_count);
        if (!all_lines) {
            const char *error_msg = "sort: read error\n";
            sys_write(2, error_msg, strlen(error_msg));
            return 1;
        }
    } 
    else {
        int has_valid_files = 0;
        
        for (int i = 0; i < num_files; i++) {
            int fd;
            
            if (filenames[i] == NULL) {
                fd = 0;
            } else {
                fd = sys_open(filenames[i], O_RDONLY, 0);
                if (fd < 0) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), "sort: cannot read: %s\n", filenames[i]);
                    sys_write(2, error_msg, strlen(error_msg));
                    return_code = 1;
                    continue;
                }
            }
            
            size_t file_line_count;
            char **file_lines = read_lines(fd, &file_line_count);
            
            if (!file_lines) {
                const char *error_msg = "sort: read error\n";
                sys_write(2, error_msg, strlen(error_msg));
                if (filenames[i] != NULL) {
                    sys_close(fd);
                }
                return_code = 1;
                continue;
            }
            
            if (file_line_count > 0) {
                char **new_all_lines = realloc(all_lines, (total_count + file_line_count) * sizeof(char *));
                if (!new_all_lines) {
                    free_lines(file_lines, file_line_count);
                    if (all_lines) free(all_lines);
                    if (filenames[i] != NULL) {
                        sys_close(fd);
                    }
                    return 1;
                }
                all_lines = new_all_lines;
                
                for (size_t j = 0; j < file_line_count; j++) {
                    all_lines[total_count++] = file_lines[j];
                }
                
                has_valid_files = 1;
            }
            
            free(file_lines);
            
            if (filenames[i] != NULL) {
                sys_close(fd);
            }
        }
        
        if (!has_valid_files && return_code != 0) {
            if (all_lines) free(all_lines);
            return return_code;
        }
    }
    
    if (total_count > 0) {
        sort_lines(all_lines, total_count, reverse);
        write_lines(1, all_lines, total_count);
    }
    
    if (all_lines) {
        free_lines(all_lines, total_count);
    }
    
    return return_code;
}

int main(int argc, char *argv[]) {
    int reverse = 0;
    case_insensitive = 0;
    int c;

    while ((c = getopt(argc, argv, "rf")) != -1) {
        switch (c) {
            case 'r':
                reverse = 1;
                break;
            case 'f':
                case_insensitive = 1;
                break;
            default:
                return 1;
        }
    }

    char **files_to_process = NULL;
    int num_files = 0;
    
    if (optind >= argc) {
        num_files = 0;
        files_to_process = NULL;
    } else {
        num_files = argc - optind;
        files_to_process = &argv[optind];
    }
    
    return process_file(files_to_process, num_files, reverse);
}