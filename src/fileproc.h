#ifndef FP_H
#define FP_H

#include "utils.h"
#include "worker.h"
#include "utils.h"

void add_path(char ***, size_t *, size_t *, const char *);

void free_paths(char**, size_t);

int copy_single_file(const char *, const char *, const char *, const char *);

int copy_files(char **, size_t, const char *, const char *);

void find_files_recursive(const char *, char ***, size_t *, size_t *);

int create_directories(const char *);

int remove_directory_recursive(const char *);

int setup_target_dir(const char *);

void start_copy(char*, char*);

#endif