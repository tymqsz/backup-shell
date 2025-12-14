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
#include "utils.h"

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))*4
#define MAX_PATH 1024

typedef struct WatchMap {
    int wd;
    char *path;
    struct WatchMap *next;
} WatchMap;

static WatchMap *watch_head = NULL;


void add_watch_mapping(int wd, const char *path) {
    WatchMap *node = malloc(sizeof(WatchMap));
    if (!node) { perror("malloc"); return; }
    node->wd = wd;
    node->path = strdup(path);
    node->next = watch_head;
    watch_head = node;
}

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
    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_MOVED_TO |
                                         IN_CLOSE_WRITE | IN_DELETE |
                                         IN_MOVED_FROM | IN_DELETE_SELF);
    
    //if (wd == -1) {
    //   
    //  
    //} else {
    add_watch_mapping(wd, path);
    //}

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

/* copy all changes in source_dir to target_dir */
void synchronize(const char *source_dir, const char *target_dir) {
    int fd = inotify_init();
    if (fd < 0)
        ERR("inotify_init");

    add_watches_recursive(fd, source_dir);

    char buffer[BUF_LEN];
    int length, i = 0;

    /* synchronize dirs while src present */
    int source_deleted = 0;
    while (!source_deleted) {
        length = read(fd, buffer, BUF_LEN); // TODO: make thread safe
        if (length < 0)
            ERR("read");

        i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if(event->len == 0){
                const char *src_dir_path = get_path_from_wd(event->wd);
                
                /* check if source_dir present */
                if(strcmp(src_dir_path, source_dir)==0 && (event->mask & IN_DELETE_SELF)){
                    source_deleted = 1;
                    break;
                }
            }

            /* file modified */
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

                    if (event->mask & IN_ISDIR) {
                        if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                            mkdir(full_tgt_path, 0777); 
                            add_watches_recursive(fd, full_src_path);
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            remove_directory_recursive(full_tgt_path);
                        }
                    } else {
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


/* return 1 if backup is already in progress otherwise 0 */
int backup_present(char* src, char* dst, workerList* workers){
    for(int i = 0; i < workers->size; i++){
        if(
            strcmp((workers->list[i]).source, src) == 0 && 
            strcmp((workers->list[i]).destination, dst) == 0
        )
            return 1;
    }

    return 0;
}

/*  verify presence of srcdir, cleanup dst 
*   and make sure backup is not present
*   returns: 0 on succes otherwise -1
*/
int prep_dirs(char* src, char* dst, workerList* workers){
    struct stat src_stat;
    if (stat(src, &src_stat) < 0 ||
        !S_ISDIR(src_stat.st_mode) ||
        backup_present(src, dst, workers) ||
        strstr(dst, src) != NULL)
        return -1;

    
    struct stat st;
    if (stat(dst, &st) == 0) {
        if (!S_ISDIR(st.st_mode))
            return -1;

        if (remove_directory_recursive(dst) != 0) {
            return -1;
        }
    }
    
    /* create the dir (use recursive ??)*/
    if (mkdir(dst, 0777) != 0) {
        return -1;
    }

    return 0;
}

/* restore files from backup_dir to restore_dir */
void restore(const char *backup_dir, const char *restore_dir) {
    DIR *b_dir = opendir(backup_dir);
    
    if (!b_dir) {
        return; 
    }

    struct dirent *dp;
    char backup_full[PATH_MAX];
    char restore_full[PATH_MAX];
    
    while ((dp = readdir(b_dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;

        snprintf(backup_full, PATH_MAX, "%s/%s", backup_dir, dp->d_name);
        snprintf(restore_full, PATH_MAX, "%s/%s", restore_dir, dp->d_name);

        struct stat b_st;
        if (lstat(backup_full, &b_st) == -1) continue;

        if (S_ISDIR(b_st.st_mode)) {
            create_directories(restore_full); 
            restore(backup_full, restore_full);
        } else {
            struct stat r_st;
            if (lstat(restore_full, &r_st) == -1 || b_st.st_mtime > r_st.st_mtime) {
                copy_single_file(backup_full, restore_full, backup_dir, restore_dir);
            }
        }
    }
    closedir(b_dir);
    
    DIR *r_dir = opendir(restore_dir);
    if (!r_dir) return; 

    while ((dp = readdir(r_dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;

        snprintf(backup_full, PATH_MAX, "%s/%s", backup_dir, dp->d_name); 
        snprintf(restore_full, PATH_MAX, "%s/%s", restore_dir, dp->d_name); 

        if (lstat(backup_full, &(struct stat){0}) == -1 && errno == ENOENT) {
            
            struct stat r_st;
            if (lstat(restore_full, &r_st) == -1) continue;

            if (S_ISDIR(r_st.st_mode)) {
                 remove_directory_recursive(restore_full);
            } else {
                 unlink(restore_full);
            }
        }
    }

    closedir(r_dir);
}