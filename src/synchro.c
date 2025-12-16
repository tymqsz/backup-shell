#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fileproc.h"
#include "utils.h"
#include "worker.h"

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16)) * 4

typedef struct WatchMap
{
    int wd;
    char *path;
    struct WatchMap *next;
} WatchMap;

static WatchMap *watch_head = NULL;

void add_watch_mapping(int wd, const char *path)
{
    WatchMap *node = malloc(sizeof(WatchMap));
    if (!node)
    {
        perror("malloc");
        return;
    }
    node->wd = wd;
    node->path = strdup(path);
    node->next = watch_head;
    watch_head = node;
}

const char *get_path_from_wd(int wd)
{
    WatchMap *current = watch_head;
    while (current)
    {
        if (current->wd == wd)
            return current->path;
        current = current->next;
    }
    return NULL;
}

void add_watches_recursive(int fd, const char *path)
{
    int wd = inotify_add_watch(fd, path,
                               IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF);
    add_watch_mapping(wd, path);

    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *dp;
    char path_buffer[PATH_MAX];
    while ((dp = readdir(dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
        {
            if (dp->d_type == DT_DIR)
            {
                snprintf(path_buffer, sizeof(path_buffer), "%s/%s", path, dp->d_name);
                add_watches_recursive(fd, path_buffer);
            }
        }
    }
    closedir(dir);
}

/* copy all changes in source_dir to target_dir */
void synchronize(const char *source_base_dir, const char *destination_base_dir)
{
    int fd = inotify_init();
    if (fd < 0)
        ERR("inotify_init");

    add_watches_recursive(fd, source_base_dir);

    char buffer[BUF_LEN];
    int length, i = 0;

    /* synchronize dirs while src present */
    int source_deleted = 0;
    while (!source_deleted)
    {
        length = read(fd, buffer, BUF_LEN);  // TODO: make thread safe
        if (length < 0)
            continue;

        i = 0;
        while (i < length)
        {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len == 0)
            {
                const char *event_source = get_path_from_wd(event->wd);

                /* check if source_dir present */
                if (strcmp(event_source, source_base_dir) == 0 && (event->mask & IN_DELETE_SELF))
                {
                    source_deleted = 1;
                    break;
                }
            }

            /* file/dir modified */
            if (event->len)
            {
                const char *event_source = get_path_from_wd(event->wd);

                if (event_source)
                {
                    char full_src_path[PATH_MAX];
                    char full_dst_path[PATH_MAX];

                    /* modifed path construction */
                    snprintf(full_src_path, sizeof(full_src_path), "%s/%s", event_source, event->name);

                    /* relative path construction */
                    const char *rel_path = event_source + strlen(source_base_dir);
                    if (*rel_path == '/')
                        rel_path++;

                    if (strlen(rel_path) > 0)
                    {
                        /* subdir */
                        snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s/%s", destination_base_dir, rel_path,
                                 event->name);
                    }
                    else
                    {
                        /* main src dir*/
                        snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s", destination_base_dir, event->name);
                    }

                    if (event->mask & IN_ISDIR)
                    {
                        /* modified path -> dir*/
                        if (event->mask & (IN_CREATE | IN_MOVED_TO))
                        {
                            mkdir(full_dst_path, 0777); /* mkdirs ? */
                            add_watches_recursive(fd, full_src_path);
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                        {
                            remove_directory_recursive(full_dst_path);
                        }
                    }
                    else
                    {
                        /* modified path -> file*/
                        if (event->mask & (IN_MOVED_TO | IN_CLOSE_WRITE))
                        {
                            char dst_cp[PATH_MAX];
                            strcpy(dst_cp, full_dst_path);
                            char *last_slash = strrchr(dst_cp, '/');
                            if (last_slash)
                            {
                                *last_slash = '\0';
                                create_directories(dst_cp);
                            }

                            copy_single_file(full_src_path, full_dst_path, source_base_dir, destination_base_dir);
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                        {
                            /* wait 20ms and check if source exists */
                            struct timespec req;
                            req.tv_sec = 0;
                            req.tv_nsec = 20 * 1000000;
                            nanosleep(&req, NULL);
                            if (access(source_base_dir, F_OK) == -1)
                            {
                                close(fd);
                                return;
                            }

                            unlink(full_dst_path);
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
int backup_present(char *src, char *dst, workerList *workers)
{
    for (int i = 0; i < workers->size; i++)
    {
        if (strcmp((workers->list[i]).source, src) == 0 && strcmp((workers->list[i]).destination, dst) == 0)
            return 1;
    }

    return 0;
}

int is_subdir(const char *child_path, const char *parent_path)
{
    char *abs_child = realpath(child_path, NULL);
    char *abs_parent = realpath(parent_path, NULL);

    int result = 0;

    if (abs_child && abs_parent)
    {
        size_t len_parent = strlen(abs_parent);
        size_t len_child = strlen(abs_child);

        if (len_parent <= len_child)
        {
            if (strncmp(abs_parent, abs_child, len_parent) == 0)
            {
                if (len_parent == 1 && abs_parent[0] == '/')
                {
                    /* parent == root */
                    result = 1;
                }
                else if (abs_child[len_parent] == '/' || abs_child[len_parent] == '\0')
                {
                    /* exclude /a/abc /a/ab */
                    result = 1;
                }
            }
        }
    }

    free(abs_child);
    free(abs_parent);

    return result;
}

/*  verify presence of srcdir and emptiness of dst
 *   and make sure backup is not present
 *   returns: 0 on succes otherwise -1
 */
int prep_dirs(char *src, char *dst, workerList *workers)
{
    /* check src exists */
    struct stat src_stat;
    if (stat(src, &src_stat) < 0 || !S_ISDIR(src_stat.st_mode) || backup_present(src, dst, workers) ||
        is_subdir(dst, src))
        return -1;

    /* check dst doesnt exist */
    struct stat st;
    if (lstat(dst, &st) == -1)
    {
        if (errno == ENOENT)
        {
            if (mkdir(dst, 0777) != 0)
            {
                return -1;
            }
            return 0;
        }
    }

    /* check dst is empty */
    if (!S_ISDIR(st.st_mode))
    {
        /* dst is not a directory*/
        return -1;
    }

    DIR *dir = opendir(dst);
    struct dirent *dp;
    if (!dir)
    {
        return -1;
    }

    while ((dp = readdir(dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
        {
            /* path other than '.' and '..' found -> its not empty*/
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

/* restore files from backup_dir to restore_dir */
void restore(const char *restore_dir, const char *backup_dir, time_t creat_time)
{
    /* handle file creation */
    DIR *b_dir = opendir(backup_dir);

    if (!b_dir)
    {
        return;
    }

    struct dirent *dp;
    char backup_full[PATH_MAX];
    char restore_full[PATH_MAX];

    while ((dp = readdir(b_dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        snprintf(backup_full, PATH_MAX, "%s/%s", backup_dir, dp->d_name);
        snprintf(restore_full, PATH_MAX, "%s/%s", restore_dir, dp->d_name);

        struct stat b_st;
        if (lstat(backup_full, &b_st) == -1)
            continue;

        if (S_ISDIR(b_st.st_mode))
        {
            create_directories(restore_full);

            /* restore recursively */
            restore(restore_full, backup_full, creat_time);
        }
        else
        {
            struct stat r_st;
            /* copy if backup is modified later */
            if (lstat(restore_full, &r_st) == -1 || (creat_time != NULL_TIME && b_st.st_mtime > creat_time))
            {
                copy_single_file(backup_full, restore_full, backup_dir, restore_dir);
            }
        }
    }
    closedir(b_dir);

    /* handle file deletion */
    DIR *r_dir = opendir(restore_dir);
    if (!r_dir)
        return;

    while ((dp = readdir(r_dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        snprintf(backup_full, PATH_MAX, "%s/%s", backup_dir, dp->d_name);
        snprintf(restore_full, PATH_MAX, "%s/%s", restore_dir, dp->d_name);

        struct stat buff;
        if (lstat(backup_full, &buff) == -1 && errno == ENOENT)
        {
            struct stat r_st;
            if (lstat(restore_full, &r_st) == -1)
                continue;

            if (S_ISDIR(r_st.st_mode))
            {
                remove_directory_recursive(restore_full);
            }
            else
            {
                unlink(restore_full);
            }
        }
    }

    closedir(r_dir);
}
