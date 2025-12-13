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

    /* TODO: change to thread safe file handling*/
    FILE *src_file, *dst_file;
    char buffer[MAX_BUF];
    size_t bytes_read;

    src_file = fopen(src, "rb");
    if (src_file == NULL) {
        perror("fopen src");
        return -1;
    }

    dst_file = fopen(dest, "wb");
    if (dst_file == NULL) {
        perror("fopen dest");
        fclose(src_file);
        return -1;
    }

    while ((bytes_read = fread(buffer, 1, MAX_BUF, src_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst_file) != bytes_read) {
            perror("fwrite");
            fclose(src_file);
            fclose(dst_file);
            return -1;
        }
    }
    
    //fchmod(fileno(dst_file), st.st_mode);

    fclose(src_file);
    fclose(dst_file);
    return 0;
}

int copy_files(char **file_paths, size_t count, const char *base_path, const char *target_dir) {
    struct stat st;

    for (size_t i = 0; i < count; i++) {
        const char *src_path = file_paths[i];
        const char *rel_path;
        char dest_path[MAX_BUF];
        char dest_dir[MAX_BUF];

        size_t base_len = strlen(base_path);
        
        /*if (strncmp(src_path, base_path, base_len) != 0) {
            continue;
        }*/

        rel_path = src_path + base_len;
        if (*rel_path == '/') {
            rel_path++;
        }

        snprintf(dest_path, MAX_BUF, "%s/%s", target_dir, rel_path);


        if (stat(src_path, &st) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (create_directories(dest_path) != 0) {
                fprintf(stderr, "Failed to create directory %s\n", dest_path);
            }
        } else {
            /* if dest path points to file -> extract parent dir*/
            char *last_slash = strrchr(dest_path, '/');
            if (last_slash != NULL) {
                size_t dir_len = last_slash - dest_path;
                strncpy(dest_dir, dest_path, dir_len);
                dest_dir[dir_len] = '\0';
                
                create_directories(dest_dir);
            }

            if (copy_single_file(src_path, dest_path, base_path, target_dir) != 0) {
                fprintf(stderr, "Failed to copy file %s\n", src_path);
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

void start_copy(char* source, char* target){
    char **file_paths = NULL;
    size_t path_count = 0;
    size_t path_capacity = 0;
    
    find_files_recursive(source, &file_paths, &path_count, &path_capacity);

    copy_files(file_paths, path_count, source, target);

    free_paths(file_paths, path_count);
}

int setup_target_dir(const char *t_path) {
    struct stat st;

    if (stat(t_path, &st) == 0) {
        
        if (!S_ISDIR(st.st_mode)) {
             return -1;
        }

        if (remove_directory_recursive(t_path) != 0) {
            return -1;
        }
    }
    
    if (mkdir(t_path, 0777) != 0) {
        return -1;
    }

    return 0;
}