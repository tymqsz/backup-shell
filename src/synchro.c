#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>

#include "fileproc.h"

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))*4
#define MAX_PATH 1024

typedef struct WatchMap {
    int wd;
    char *path;
    struct WatchMap *next;
} WatchMap;

static WatchMap *watch_head = NULL;

// Dodaje mapowanie wd -> path
void add_watch_mapping(int wd, const char *path) {
    WatchMap *node = malloc(sizeof(WatchMap));
    if (!node) { perror("malloc"); return; }
    node->wd = wd;
    node->path = strdup(path);
    node->next = watch_head;
    watch_head = node;
}

// Pobiera ścieżkę na podstawie wd
const char* get_path_from_wd(int wd) {
    WatchMap *current = watch_head;
    while (current) {
        if (current->wd == wd) return current->path;
        current = current->next;
    }
    return NULL;
}

// Rekurencyjne dodawanie obserwacji (inotify watch) dla katalogów
void add_watches_recursive(int fd, const char *path) {
    // Dodajemy watch na obecny katalog (IN_CREATE wykryje nowe pliki/foldery, IN_CLOSE_WRITE zmiany w plikach)
    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM);
    
    if (wd == -1) {
        // Ignorujemy błędy dla plików, które nie są katalogami (chociaż opendir niżej i tak to przefiltruje)
        // lub brak uprawnień
    } else {
        add_watch_mapping(wd, path);
    }

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *dp;
    char path_buffer[MAX_PATH];

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            if (dp->d_type == DT_DIR) {
                snprintf(path_buffer, sizeof(path_buffer), "%s/%s", path, dp->d_name);
                add_watches_recursive(fd, path_buffer);
            }
        }
    }
    closedir(dir);
}




void synchronize(const char *source_dir, char **target_dirs) {
    int fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return;
    }
    
    add_watches_recursive(fd, source_dir);

    char buffer[BUF_LEN];
    int length, i = 0;

    while (1) {
        length = read(fd, buffer, BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }

        i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            
            if (event->len) {
                const char *src_dir_path = get_path_from_wd(event->wd);
                
                if (src_dir_path) {
                    char full_src_path[MAX_PATH];
                    
                    // 1. Construct Source Path
                    snprintf(full_src_path, sizeof(full_src_path), "%s/%s", src_dir_path, event->name);

                    // 2. Calculate Relative Path
                    const char *rel_path = src_dir_path + strlen(source_dir);
                    if (*rel_path == '/') rel_path++;

                    // 3. Handle Source-Side Logic (Watchers) - Only once per event
                    if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                        add_watches_recursive(fd, full_src_path);
                    }

                    // 4. Iterate over ALL target directories
                    for (int t = 0; target_dirs[t] != NULL; t++) {
                        char full_tgt_path[MAX_PATH];
                        const char *current_target_root = target_dirs[t];

                        // Construct Target Path
                        if (strlen(rel_path) > 0) {
                            snprintf(full_tgt_path, sizeof(full_tgt_path), "%s/%s/%s", current_target_root, rel_path, event->name);
                        } else {
                            snprintf(full_tgt_path, sizeof(full_tgt_path), "%s/%s", current_target_root, event->name);
                        }

                        // 5. Handle Target-Side Logic (Replication)
                        if (event->mask & IN_ISDIR) {
                            if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                                mkdir(full_tgt_path, 0777); 
                            }
                            else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                                remove_directory_recursive(full_tgt_path);
                            }
                        } else {
                            // --- FILE HANDLING ---
                            if (event->mask & (IN_MOVED_TO | IN_CLOSE_WRITE)) {
                                char dest_dir_copy[4096];
                                strcpy(dest_dir_copy, full_tgt_path);
                                char *last_slash = strrchr(dest_dir_copy, '/');
                                if (last_slash) {
                                    *last_slash = '\0';
                                    create_directories(dest_dir_copy);
                                }
                                // Pass the specific target root for this iteration
                                copy_single_file(full_src_path, full_tgt_path, source_dir, current_target_root);
                            }
                            else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                                unlink(full_tgt_path);
                            }
                        }
                    } 
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }
    close(fd);
}