#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>

#include "worker.h"
#include "synchro.h"
#include "fileproc.h"

void add_worker(char* src, char** dsts, int dst_cnt, pid_t pid, workerList* workers){
    if(workers->size >= workers->capacity){
        worker* new_list = realloc(workers->list, (workers->capacity+16)*sizeof(worker));
        if(new_list == NULL)
            exit(EXIT_FAILURE);
        
        workers->list = new_list;
        workers->capacity += 16;
    }
    
    workers->list[workers->size].source = strdup(src);
    workers->list[workers->size].dst_count = dst_cnt;
    workers->list[workers->size].source = strdup(src);


    if (workers->list[workers->size].source == NULL) exit(EXIT_FAILURE);

    // 3. Deep Copy the Destinations Array (CRITICAL FIX)
    // Allocate memory for the array of pointers (+1 for the NULL terminator)
    char **dst_copy = malloc((dst_cnt + 1) * sizeof(char*));
    if (dst_copy == NULL) exit(EXIT_FAILURE);

    // Copy every string individually
    for (int i = 0; i < dst_cnt; i++) {
        dst_copy[i] = strdup(dsts[i]);
        if (dst_copy[i] == NULL) exit(EXIT_FAILURE);
    }
    dst_copy[dst_cnt] = NULL; // Ensure it is NULL-terminated

    // Assign the NEW copy to the struct
    workers->list[workers->size].destinations = dst_copy;


    workers->list[workers->size].pid = pid;

    workers->size++;
}

void init_workerList(workerList** workers){
    *workers = malloc(sizeof(workerList));
    (*workers)->capacity = 16;
    (*workers)->size = 0;
    (*workers)->list = malloc(sizeof(worker) * (*workers)->capacity);
}

void display_workerList(workerList* workers){
    for (int i = 0; i < workers->size; i++) {
        printf("synch: %s | targets: ", workers->list[i].source);

        if (workers->list[i].destinations != NULL) {
            int j = 0;
            while (workers->list[i].destinations[j] != NULL) {
                printf("[%s] ", workers->list[i].destinations[j]);
                j++;
            }
        } else {
            printf("(none)");
        }
        printf("\n");
    }

}



void child_work(char* src, char** dst){
    setup_target_dirs(dst);

    start_copy(src, dst);
    
    synchronize(src, dst);

    kill(getppid(), SIGUSR1);
}

int synchro_present(char* src, char* dst, workerList* workers){
    for(int i = 0; i < workers->size; i++){
        worker w = workers->list[i];
        for(int j = 0; j < w.dst_count; j++){
            if(
                strcmp((w).source, src) == 0 && 
                strcmp((w).destinations[j], dst) == 0
            )
                return 1;
        }
    }

    return 0;
}

int dst_is_subdir(char* src, char* dst){
    return strstr(dst, src) != NULL;
}

int prep_dirs(char* src, char** t_paths, workerList* workers) {
    // --- 1. Validate Source ---
    struct stat src_stat;
    if (stat(src, &src_stat) < 0 || !S_ISDIR(src_stat.st_mode)) {
        fprintf(stderr, "Error: Source %s is invalid or not a directory.\n", src);
        return 0;
    }

    // --- 2. Validate ALL Targets (Safety Check) ---
    // We check all targets *before* we delete anything.
    int i = 0;
    while (t_paths[i] != NULL) {
        char *current_dst = t_paths[i];

        // Pass the CURRENT target (t_paths[i]) to your validation functions
        if (synchro_present(src, current_dst, workers)) {
            fprintf(stderr, "Error: Synchronization already active for %s -> %s\n", src, current_dst);
            return 0;
        }

        if (dst_is_subdir(src, current_dst)) {
            fprintf(stderr, "Error: Target %s is a subdirectory of source %s (recursion risk).\n", current_dst, src);
            return 0;
        }
        i++;
    }

    // --- 3. Prepare Targets (Destructive Action) ---
    // Now that we know all targets are safe, we proceed to wipe and recreate them.
    i = 0;
    while (t_paths[i] != NULL) {
        const char *current_path = t_paths[i];
        struct stat st;

        // Check if the path exists
        if (stat(current_path, &st) == 0) {
            // If it exists but is NOT a directory, abort
            if (!S_ISDIR(st.st_mode)) {
                 fprintf(stderr, "Error: '%s' exists but is not a directory.\n", current_path);
                 return 0;
            }

            // It exists and is a directory: wipe it clean
            if (remove_directory_recursive(current_path) != 0) {
                fprintf(stderr, "Error: Failed to clean up '%s'.\n", current_path);
                return 0;
            }
        }
        
        // Create the fresh directory
        if (mkdir(current_path, 0777) != 0) {
            perror("mkdir");
            return 0;
        }

        i++;
    }

    return 1; // Success
}