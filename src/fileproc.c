#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fileproc.h"
#include "utils.h"
#include "worker.h"

#define MAX_PATH 1024
#define MAX_BUF 1024

int remove_directory_recursive(const char *path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = 0;

    if (!d)
    {
        return 0;
    }

    struct dirent *p;
    while ((p = readdir(d)))
    {
        int r2;
        char *buf;
        size_t len;

        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
            continue;

        len = path_len + strlen(p->d_name) + 2;
        buf = malloc(len);

        if (buf)
        {
            snprintf(buf, len, "%s/%s", path, p->d_name);
            struct stat statbuf;

            /* ignore symlink destinations */
            if (lstat(buf, &statbuf) == -1)
            {
                perror("lstat");
                r = -1;
                free(buf);
                continue;
            }

            if (S_ISDIR(statbuf.st_mode))
            {
                r2 = remove_directory_recursive(buf);
            }
            else
            {
                r2 = unlink(buf);
            }

            if (r2)
                r = -1;
            free(buf);
        }
    }
    closedir(d);

    if (r == 0)
    {
        r = rmdir(path);
    }

    return r;
}
/* save path to paths array*/
void add_path(char ***paths, size_t *count, size_t *capacity, const char *path)
{
    if (*count >= *capacity)
    {
        if (*capacity == 0)
            *capacity = 5;
        *capacity = *capacity * 2;

        char **new_paths = realloc(*paths, *capacity * sizeof(char *));
        if (new_paths == NULL)
        {
            ERR("realloc");
        }
        *paths = new_paths;
    }

    (*paths)[*count] = strdup(path);
    if ((*paths)[*count] == NULL)
    {
        ERR("strdup");
        return;
    }

    (*count)++;
}

/* find all files in a crt_path directory*/
void find_files_recursive(const char *crt_path, char ***paths, size_t *count, size_t *capacity)
{
    char file[PATH_MAX];
    struct dirent *dp;
    DIR *dir = opendir(crt_path);

    if (!dir)
        return;

    while ((dp = readdir(dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
        {
            snprintf(file, sizeof(file), "%s/%s", crt_path, dp->d_name);
            add_path(paths, count, capacity, file);

            /* if dirent is a directory search deeper*/
            if (dp->d_type == DT_DIR)
            {
                find_files_recursive(file, paths, count, capacity);
            }
        }
    }
    closedir(dir);
}

int create_directories(const char *path)
{
    char temp_path[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(temp_path, sizeof(temp_path), "%s", path);
    len = strlen(temp_path);

    /* ensure creating last directory */
    if (temp_path[len - 1] == '/')
    {
        temp_path[len - 1] = '\0';
    }

    for (p = temp_path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            /* create middle directory */
            if (mkdir(temp_path, 0777) && errno != EEXIST)
            {
                return -1;
            }

            /* restore previus path string*/
            *p = '/';
        }
    }

    /* create final directory */
    if (mkdir(temp_path, 0777) && errno != EEXIST)
    {
        return -1;
    }

    return 0;
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;  // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int copy_single_file(const char *src, const char *dest, const char *base_src, const char *base_dest)
{
    struct stat st;

    if (lstat(src, &st) == -1)
    {
        ERR("lstat");
        return -1;
    }
    if (S_ISLNK(st.st_mode))
    {
        char link_target[MAX_BUF];
        char final_target[MAX_BUF];
        ssize_t len;

        len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len == -1)
        {
            ERR("readlink error");
            return -1;
        }
        link_target[len] = '\0';

        /* if link points to local file change target accordingly */
        size_t base_src_len = strlen(base_src);
        if (link_target[0] == '/' && strncmp(link_target, base_src, base_src_len) == 0)
        {
            // base="/src", link="/src_backup/file"
            if (link_target[base_src_len] == '/' || link_target[base_src_len] == '\0')
            {
                const char *suffix = link_target + base_src_len;
                snprintf(final_target, sizeof(final_target), "%s%s", base_dest, suffix);
            }
            else
            {
                /* link is external */
                strcpy(final_target, link_target);
            }
        }
        else
        {
            /* link is external */
            strcpy(final_target, link_target);
        }

        if (symlink(final_target, dest) == -1)
        {
            ERR("symlink");
            return -1;
        }

        return 0;
    }

    if (S_ISDIR(st.st_mode))
    {
        return 0;
    }

    int src_fd, dst_fd;
    char buffer[MAX_BUF];
    ssize_t bytes_read, bytes_written;

    src_fd = TEMP_FAILURE_RETRY(open(src, O_RDONLY));
    if (src_fd < 0)
    {
        ERR("open src");
        return -1;
    }

    dst_fd = TEMP_FAILURE_RETRY(open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0777));
    if (dst_fd < 0)
    {
        ERR("open dest");
        TEMP_FAILURE_RETRY(close(src_fd));
        return -1;
    }

    while ((bytes_read = bulk_read(src_fd, buffer, MAX_BUF)) > 0)
    {
        bytes_written = bulk_write(dst_fd, buffer, bytes_read);

        if (bytes_written != bytes_read)
        {
            ERR("bulk_write");
            break;
        }
    }

    if (bytes_read < 0)
    {
        ERR("bulk_read");
        return -1;
    }

    if (TEMP_FAILURE_RETRY(close(src_fd)) < 0)
    {
        ERR("close src");
        return -1;
    }

    if (TEMP_FAILURE_RETRY(close(dst_fd)) < 0)
    {
        ERR("close dst");
        return -1;
    }
    return 0;
}

int copy_files(char **file_paths, size_t count, const char *base_path, const char *target_dir)
{
    struct stat st;

    for (size_t i = 0; i < count; i++)
    {
        const char *src_path = file_paths[i];
        const char *rel_path;
        char dest_path[PATH_MAX];
        char dest_dir[PATH_MAX];

        size_t base_len = strlen(base_path);

        rel_path = src_path + base_len;
        if (*rel_path == '/')
        {
            rel_path++;
        }

        snprintf(dest_path, MAX_BUF, "%s/%s", target_dir, rel_path);

        if (stat(src_path, &st) == -1)
        {
            ERR("stat");
            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            if (create_directories(dest_path) != 0)
            {
                ERR("create_directories");
            }
        }
        else
        {
            /* if dest path points to file -> extract parent dir*/
            char *last_slash = strrchr(dest_path, '/');
            if (last_slash != NULL)
            {
                size_t dir_len = last_slash - dest_path;
                strncpy(dest_dir, dest_path, dir_len);
                dest_dir[dir_len] = '\0';

                /* create parent dirs */
                create_directories(dest_dir);
            }

            if (copy_single_file(src_path, dest_path, base_path, target_dir) != 0)
            {
                ERR("copy_single_file");
            }
        }
    }
    return 0;
}

void free_paths(char **paths, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        free(paths[i]);
    }
    free(paths);
}

void start_copy(char *source, char *target)
{
    char **file_paths = NULL;
    size_t path_count = 0;
    size_t path_capacity = 0;

    find_files_recursive(source, &file_paths, &path_count, &path_capacity);

    copy_files(file_paths, path_count, source, target);

    free_paths(file_paths, path_count);
}

int setup_target_dir(const char *t_path)
{
    struct stat st;

    if (stat(t_path, &st) == 0)
    {
        if (!S_ISDIR(st.st_mode))
        {
            return -1;
        }

        if (remove_directory_recursive(t_path) != 0)
        {
            return -1;
        }
    }

    if (mkdir(t_path, 0777) != 0)
    {
        return -1;
    }

    return 0;
}
