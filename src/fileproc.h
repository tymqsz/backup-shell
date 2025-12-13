#ifndef FP_H

#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024
#define FP_H


void add_path(char ***paths, size_t *count, size_t *capacity, const char *path);

void find_files_recursive(const char *basePath, char ***paths, size_t *count, size_t *capacity);

int create_directories(const char *path);

int copy_single_file(const char *src, const char *dest, const char *base_src, const char *base_dest);

int copy_files(char **file_paths, size_t count, const char *base_path, const char *target_dir);

void free_paths(char**, size_t);

int remove_directory_recursive(const char *path);

int setup_target_dir(const char *t_path);

void start_copy(char* source, char* target);
#endif