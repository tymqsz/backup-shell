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
#include "worker.h"

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
    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF);
    
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




void synchronize(const char *source_dir, const char *target_dir) {
    int fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return;
    }
    
    /* stworz watchery */
    add_watches_recursive(fd, source_dir);

    char buffer[BUF_LEN];
    int length, i = 0;

    

    int source_deleted = 0;
    while (!source_deleted) {
        length = read(fd, buffer, BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }

        i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if(event->len == 0){
                const char *src_dir_path = get_path_from_wd(event->wd);

                if(strcmp(src_dir_path, source_dir)==0 && (event->mask & IN_DELETE_SELF)){
                    source_deleted = 1;
                    break;
                }
            }

            if (event->len) {
                const char *src_dir_path = get_path_from_wd(event->wd);
                
                if (src_dir_path) {
                    char full_src_path[MAX_PATH];
                    char full_tgt_path[MAX_PATH];
                    
                    // Konstrukcja pełnej ścieżki źródłowej
                    snprintf(full_src_path, sizeof(full_src_path), "%s/%s", src_dir_path, event->name);

                    // Obliczenie ścieżki względnej
                    const char *rel_path = src_dir_path + strlen(source_dir);
                    if (*rel_path == '/') rel_path++;

                    // Sprawdzamy, czy jesteśmy w podfolderze, czy w głównym katalogu
                    if (strlen(rel_path) > 0) {
                        // Jesteśmy w podkatalogu: target + / + rel_path + / + name
                        snprintf(full_tgt_path, sizeof(full_tgt_path), "%s/%s/%s", target_dir, rel_path, event->name);
                    } else {
                        // Jesteśmy w katalogu głównym: target + / + name (bez rel_path pośrodku)
                        snprintf(full_tgt_path, sizeof(full_tgt_path), "%s/%s", target_dir, event->name);
                    }
                    // ---------------------
                    
                    /* tworzenie dwoch dir naraz */


                    // Logika obsługi zdarzeń
                    if (event->mask & IN_ISDIR) {
                        if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                            mkdir(full_tgt_path, 0777); 
                            add_watches_recursive(fd, full_src_path);
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            remove_directory_recursive(full_tgt_path);
                        }
                    } else {
                        // --- OBSŁUGA PLIKÓW ---
                        if (event->mask & (IN_MOVED_TO | IN_CLOSE_WRITE)) {
                            
                            char dest_dir_copy[4096];
                            strcpy(dest_dir_copy, full_tgt_path);
                            char *last_slash = strrchr(dest_dir_copy, '/');
                            if (last_slash) {
                                *last_slash = '\0';
                                create_directories(dest_dir_copy);
                            }

                            copy_single_file(full_src_path, full_tgt_path, source_dir, target_dir);
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            // Usuwanie pliku
                            unlink(full_tgt_path);
                        }
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }
    
    close(fd);
}

int prep_dirs(char* src, char* dst, workerList* workers) {
    // --- 1. Validate Source ---
    struct stat src_stat;
    if (stat(src, &src_stat) < 0 || !S_ISDIR(src_stat.st_mode)) {
        fprintf(stderr, "Error: Source %s is invalid or not a directory.\n", src);
        return 0;
    }

    // --- 2. Validate Target (Safety Check) ---
    // Check if this specific synchronization is already running
    if (synchro_present(src, dst, workers)) {
        fprintf(stderr, "Error: Synchronization already active for %s -> %s\n", src, dst);
        return 0;
    }

    // Check for recursion (if destination is inside source)
    if (dst_is_subdir(src, dst)) {
        fprintf(stderr, "Error: Target %s is a subdirectory of source %s (recursion risk).\n", dst, src);
        return 0;
    }

    // --- 3. Prepare Target (Destructive Action) ---
    // Now that we know the target is safe, we proceed to wipe and recreate it.
    struct stat st;

    // Check if the path exists
    if (stat(dst, &st) == 0) {
        // If it exists but is NOT a directory, abort
        if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "Error: '%s' exists but is not a directory.\n", dst);
                return 0;
        }

        // It exists and is a directory: wipe it clean
        if (remove_directory_recursive(dst) != 0) {
            fprintf(stderr, "Error: Failed to clean up '%s'.\n", dst);
            return 0;
        }
    }
    
    // Create the fresh directory
    if (mkdir(dst, 0777) != 0) {
        perror("mkdir");
        return 0;
    }

    return 1; // Success
}