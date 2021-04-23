#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <libgen.h>

#include "./gameplay/reversi.h"
#include "./hashing/hash_functions.h"
#include "./mem_man/heir.hpp"
#include "./gameplay/walker.hpp"
#include "./utils/tarraylist.hpp"
#include "./utils/pqueue.hpp"
#include "./utils/fileio.hpp"
#include "./utils/path_util.h"
#include "./utils/csv.h"
#include "./gameplay/reversi_defs.h"
#include "./utils/gui.hpp"

// TODO you can use the previous two board states to predict the next set of valid moves.

/**
 * 
 * use 'ulimit -c unlimited' to make core dump
 * 
 * 
 */

// TODO Maybe try to use heroseh's gui stuff to make interface look nice

uint8_t SHUTDOWN_FLAG = 0;
pthread_mutex_t shutdown_lock;

void graceful_shutdown(int sig) {
    pthread_mutex_lock(&shutdown_lock);
    if(sig == SIGINT) {
        printf("Shutting down processor\n");
        if(SHUTDOWN_FLAG) exit(0);
        SHUTDOWN_FLAG = 1;
    }
    pthread_mutex_unlock(&shutdown_lock);
}

int main() {

    // This way we can remove the mmapped files
    signal(SIGINT, graceful_shutdown);

    SHUTDOWN_FLAG = 1;

    setlocale(LC_NUMERIC, "");

    char* temp_dir = (char*)((getenv("TEMP_DIR")) ? getenv("TEMP_DIR") : "/tmp");

    #ifndef reusefiles
        char* temp_result = (char*)malloc(sizeof(char) * (strlen(temp_dir) + 16));
        snprintf(temp_result, strlen(temp_dir) + 16, "%s/reversi.XXXXXX", temp_dir);
        temp_dir = mkdtemp(temp_result);
    #endif

    char* checkpoint_filename, *csv_filename;

    if(!getenv("CHECKPOINT_PATH")) {
        checkpoint_filename = (char*)malloc(sizeof(char) * (strlen(temp_dir) + 16));
        snprintf(checkpoint_filename, strlen(temp_dir) + 16, "%s/checkpoint.bin", temp_dir);
    }
    else checkpoint_filename = getenv("CHECKPOINT_PATH");

    csv_filename = (char*)malloc(sizeof(char) * (strlen(temp_dir) + 9));
    if(!csv_filename) err(1, "Memory error while allocating csv_filename\n");
    snprintf(csv_filename, strlen(temp_dir) + 16, "%s/log.csv", temp_dir);

    // Allocate all of the stack parameters necessary for file restoration
    heirarchy cache;
    uint64_t count = 0, explored_count = 1, repeated_count = 0;
    size_t finished_count = 0;
    // char* checkpoint_filename;

    // Calculate the number of processors to use
    uint32_t procs = get_nprocs();
    #ifndef limitprocs
        procs = procs << 1;
    #else
        procs = 1;
    #endif

    // Setup the locks
    pthread_mutex_t counter_lock, explored_lock, file_lock, repeated_lock, finished_lock;

    // Setup the checkpoint saving system
    FILE** checkpoint_file = (FILE**)malloc(sizeof(FILE*));
    if(!checkpoint_file) err(1, "Memory error while allocating checkpoint file pointer\n");
    uint64_t saving_counter;

    // Initialize the locks
    if(pthread_mutex_init(&counter_lock, 0) || pthread_mutex_init(&explored_lock, 0) || pthread_mutex_init(&repeated_lock, 0) || 
       pthread_mutex_init(&file_lock, 0) || pthread_mutex_init(&saving_lock, 0) || pthread_mutex_init(&shutdown_lock, 0) || 
       pthread_mutex_init(&heirarchy_lock, 0) || pthread_mutex_init(&finished_lock, 0) || pthread_mutex_init(&heirarchy_cache_lock, 0)) 
        err(4, "Initialization of counter mutex failed\n");

    Arraylist<void*>* threads;

    #pragma region determine if loading checkpoint

    if(ask_yes_no("Would you like to restore from a checkpoint?")) {
        char** restore_filename = (char**)malloc(sizeof(char*));
        printf("Please enter a file to restore from: ");
        scanf("%ms", restore_filename);
        getc(stdin);    // Read the extra \n character
        processed_file pf = restore_progress_v2(*restore_filename);

        #ifndef skipconfirm
            if(ask_yes_no("Would you like to continue saving to this checkpoint?")) {
                checkpoint_filename = (getenv("CHECKPOINT_PATH")) ? getenv("CHECKPOINT_PATH") : checkpoint_filename; // find_temp_filename("checkpoint.bin\0");
            }
            else {
                checkpoint_filename = (char*)malloc(sizeof(char) * strlen(*restore_filename));
                strcpy(checkpoint_filename, *restore_filename);
                csv_filename = (char*)malloc(sizeof(char) * (strlen(pf->cache->final_level->file_directory) + 9));
                if(!csv_filename) err(1, "Memory error while allocating csv_filename\n");
                temp_dir = (char*)malloc(sizeof(char) * strlen(checkpoint_filename));
                if(!temp_dir) err(1, "Memory error while allocating csv_filename\n");
                strcpy(temp_dir, checkpoint_filename);
                temp_dir = dirname(temp_dir);
                snprintf(csv_filename, strlen(temp_dir) + 16, "%s/log.csv", temp_dir);
            }
        #else
            checkpoint_filename = malloc(sizeof(char) * strlen(*restore_filename));
            strcpy(checkpoint_filename, *restore_filename);
            csv_filename = malloc(sizeof(char) * (strlen(pf->cache->final_level->file_directory) + 9));
            if(!csv_filename) err(1, "Memory error while allocating csv_filename\n");
            temp_dir = malloc(sizeof(char) * strlen(checkpoint_filename));
            if(!temp_dir) err(1, "Memory error while allocating csv_filename\n");
            strcpy(temp_dir, checkpoint_filename);
            temp_dir = dirname(temp_dir);
            snprintf(csv_filename, strlen(temp_dir) + 16, "%s/log.csv", temp_dir);
        #endif

        free(*restore_filename);

        // Begin restore
        count = pf->found_counter;
        explored_count = pf->explored_counter;
        cache = pf->cache;
    }
    
    pthread_t scheduler;
    LockedPriorityQueue<board>* schedulerq = new LockedPriorityQueue<board>(1000);
    processor_scheduler_args_t* schargs = create_processor_scheduler_args(cache, schedulerq, procs,
                                                                          &count, &counter_lock,
                                                                          &explored_count, &explored_lock,
                                                                          &repeated_count, &repeated_lock,
                                                                          &saving_counter, checkpoint_file, &file_lock,
                                                                          &finished_count, &finished_lock);
    pthread_create(&scheduler, 0, walker_task_scheduler, schargs);

    schedulerq->push(create_board(1, BOARD_HEIGHT, BOARD_WIDTH, 0));

    #pragma endregion

    printf("Starting walk...\n");
    printf("Running on %d threads\n", procs);
    printf("Saving checkpoints to %s\n", checkpoint_filename);

    // for(uint64_t t = 0; t < threads->pointer; t++) pthread_join(*(pthread_t*)(threads->data[0]), 0);
    SHUTDOWN_FLAG = 0;
    time_t current, save_timer = time(0);
    uint32_t save_time;

    initialize_main_loop_display(csv_filename, cache, &count, &explored_count);

    while(1) {
        display_main_loop();

        #ifdef fastsave
            save_time = (current - save_timer) / 5;
        #else
            save_time = (current - save_timer) / 3600;
        #endif

        if(finished_count == procs) break;

        if(save_time) {
            printf(" Saving...\n");
            // SHUTDOWN_FLAG = 1;
            save_progress_v2(checkpoint_file, &file_lock, checkpoint_filename, &saving_counter, cache, count, explored_count, repeated_count, procs - finished_count);
            save_timer = time(0);
        }

        if(SHUTDOWN_FLAG) {
            fflush(stdout);
            save_progress_v2(checkpoint_file, &file_lock, checkpoint_filename, &saving_counter, cache, count, explored_count, repeated_count, procs - finished_count);
            WALKER_KILL_FLAG = 1;
            while(finished_count < procs) sched_yield();
            destroy_heirarchy(cache);
            exit(0);
        }
        else {
            fflush(stdout);
            sched_yield();
        }
    }

    fflush(stdout);
    // save_progress_v2(checkpoint_file, &file_lock, checkpoint_filename, &saving_counter, cache, count, explored_count, repeated_count, procs - finished_count);
    destroy_heirarchy(cache);

    printf("\nThere are %ld possible board states\n", count);
}
