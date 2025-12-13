#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PATH 1024
#define MAX_BUF 1024

/* save path to paths array*/
void add_path(char ***paths, size_t *count, size_t *capacity, const char *path) {
    if (*count >= *capacity) {
        if(*capacity == 0)
            *capacity = 5;
        *capacity = *capacity * 2;

        char **new_paths = realloc(*paths, *capacity * sizeof(char*));
        if (new_paths == NULL) {
            perror("realloc");
            return;
        }
        *paths = new_paths;
    }

    (*paths)[*count] = strdup(path);
    if ((*paths)[*count] == NULL) {
        perror("strdup");
        return;
    }
    
    (*count)++;
}

/* find all files in a crt_path directory*/
void find_files_recursive(const char *crt_path, char ***paths, size_t *count, size_t *capacity) {
    char file[MAX_PATH];
    struct dirent *dp;
    DIR *dir = opendir(crt_path);

    if (!dir)
        return;

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            
            snprintf(file, sizeof(file), "%s/%s", crt_path, dp->d_name);
            add_path(paths, count, capacity, file);
            
            /* if dirent is a directory search deeper*/
            if (dp->d_type == DT_DIR) {
                find_files_recursive(file, paths, count, capacity);
            }
        }
    }
    closedir(dir);
}

int create_directories(const char *path) {
    char temp_path[MAX_PATH];
    char *p = NULL;
    size_t len;

    snprintf(temp_path, sizeof(temp_path), "%s", path);
    len = strlen(temp_path);

    /* ensure creating last directory */
    if (temp_path[len - 1] == '/') {
        temp_path[len - 1] = '\0';
    }

    for (p = temp_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            
            /* create middle directory */
            if (mkdir(temp_path, 0777) && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    /* create final directory */
    if (mkdir(temp_path, 0777) && errno != EEXIST) {
        return -1;
    }

    return 0;
}

ssize_t bulk_read(int fd, char *buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len; // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count) {
    ssize_t c;
    ssize_t len = 0;
    do {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int copy_single_file(const char *src, const char *dest, const char *base_src, const char *base_dest) {
    struct stat st;
    
    if (lstat(src, &st) == -1) {
        perror("lstat error");
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        char link_target[MAX_BUF];
        char final_target[MAX_BUF];
        ssize_t len;

        len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len == -1) {
            perror("readlink error");
            return -1;
        }
        link_target[len] = '\0';

        /* if link points to local file change target accordingly */
        size_t base_src_len = strlen(base_src);
        if (link_target[0] == '/' && strncmp(link_target, base_src, base_src_len) == 0) {
            // base="/src", link="/src_backup/file"
            if (link_target[base_src_len] == '/' || link_target[base_src_len] == '\0') {
                
                const char *suffix = link_target + base_src_len;
                snprintf(final_target, sizeof(final_target), "%s%s", base_dest, suffix);
            } else {
                /* link is external */
                strcpy(final_target, link_target);
            }
        } else {
            /* link is external */
            strcpy(final_target, link_target);
        }

        
        //unlink(dest);

        if (symlink(final_target, dest) == -1) {
            perror("symlink creation failed");
            return -1;
        }
        
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        return 0;
    }

    int src_fd, dst_fd;
    char buffer[MAX_BUF];
    ssize_t bytes_read, bytes_written;
    int result = 0;

    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("open src");
        return -1;
    }

    dst_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0) {
        perror("open dest");
        close(src_fd);
        return -1;
    }

    while ((bytes_read = bulk_read(src_fd, buffer, MAX_BUF)) > 0) {
        bytes_written = bulk_write(dst_fd, buffer, bytes_read);
        
        if (bytes_written != bytes_read) {
            perror("bulk_write");
            result = -1;
            break; 
        }
    }

    if (bytes_read < 0) {
        perror("bulk_read");
        result = -1;
    }

    if (close(src_fd) < 0) {
        perror("close src");
        result = -1;
    }
    
    if (close(dst_fd) < 0) {
        perror("close dst");
        result = -1;
    }
    return 0;
}

int copy_files(char **file_paths, size_t count, const char *base_path, char **target_dirs) {
    struct stat st;

    for (size_t i = 0; i < count; i++) {
        const char *src_path = file_paths[i];
        
        // Optimization: Stat the source once before looping through targets
        if (stat(src_path, &st) == -1) {
            perror("stat");
            continue;
        }

        // Calculate relative path once
        size_t base_len = strlen(base_path);
        const char *rel_path = src_path + base_len;
        if (*rel_path == '/') {
            rel_path++;
        }

        // --- Inner Loop: Iterate through all target directories ---
        for (int t = 0; target_dirs[t] != NULL; t++) {
            const char *current_target_root = target_dirs[t];
            char dest_path[MAX_BUF];
            char dest_dir[MAX_BUF];

            // Construct specific destination path for this target
            snprintf(dest_path, MAX_BUF, "%s/%s", current_target_root, rel_path);

            if (S_ISDIR(st.st_mode)) {
                // Case: Directory
                if (create_directories(dest_path) != 0) {
                    fprintf(stderr, "Failed to create directory %s\n", dest_path);
                }
            } else {
                // Case: File
                /* extract parent dir from dest_path */
                char *last_slash = strrchr(dest_path, '/');
                if (last_slash != NULL) {
                    size_t dir_len = last_slash - dest_path;
                    strncpy(dest_dir, dest_path, dir_len);
                    dest_dir[dir_len] = '\0';
                    
                    create_directories(dest_dir);
                }

                // Copy the file to this specific target
                if (copy_single_file(src_path, dest_path, base_path, current_target_root) != 0) {
                    fprintf(stderr, "Failed to copy file %s to %s\n", src_path, dest_path);
                }
            }
        }
    }
    return 0;
}

void free_paths(char **paths, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(paths[i]);
    }
    free(paths);
}

int remove_directory_recursive(const char *path) {
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = 0;

    if (!d) {
        return 0; 
    }

    struct dirent *p;
    while ((p = readdir(d))) {
        int r2;
        char *buf;
        size_t len;

        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

        len = path_len + strlen(p->d_name) + 2; 
        buf = malloc(len);

        if (buf) {
            snprintf(buf, len, "%s/%s", path, p->d_name);
            struct stat statbuf;
            
            // Używamy lstat, żeby nie podążać za dowiązaniami symbolicznymi (chyba że są w katalogu do usunięcia)
            if (lstat(buf, &statbuf) == -1) {
                perror("lstat");
                r = -1;
                free(buf);
                continue;
            }

            if (S_ISDIR(statbuf.st_mode)) {
                r2 = remove_directory_recursive(buf); // REKURENCJA
            } else {
                r2 = unlink(buf); // Usuwamy plik/link
            }
            
            if (r2) r = -1;
            free(buf);
        }
    }
    closedir(d);

    if (r == 0) {
        r = rmdir(path);
    }
    
    return r;
}

void start_copy(char* source, char** targets){
    char **file_paths = NULL;
    size_t path_count = 0;
    size_t path_capacity = 0;
    
    find_files_recursive(source, &file_paths, &path_count, &path_capacity);

    copy_files(file_paths, path_count, source, targets);

    free_paths(file_paths, path_count);
}

int setup_target_dirs(const char **t_paths) {
    struct stat st;
    int i = 0;

    // Loop until we hit the NULL terminator in the array
    while (t_paths[i] != NULL) {
        const char *current_path = t_paths[i];

        // Check if the path exists
        if (stat(current_path, &st) == 0) {
            
            // If it exists but is NOT a directory, that's an error
            if (!S_ISDIR(st.st_mode)) {
                 fprintf(stderr, "Error: '%s' exists but is not a directory.\n", current_path);
                 return -1;
            }

            // It exists and is a directory: wipe it clean
            if (remove_directory_recursive(current_path) != 0) {
                fprintf(stderr, "Error: Failed to clean up '%s'.\n", current_path);
                return -1;
            }
        }
        
        // Create the fresh directory
        if (mkdir(current_path, 0777) != 0) {
            perror("mkdir");
            return -1;
        }

        i++;
    }

    return 0;
}