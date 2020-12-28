#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <string.h>

#include "./gameplay/reversi.h"
#include "./hashing/hash_functions.h"
#include "./mem_man/heir.h"
#include "./gameplay/walker.h"
#include "./utils/ll.h"
#include "./utils/arraylist.h"
#include "./gameplay/valid_moves.h"
#include "./utils/fileio.h"

// TODO you can use the previous two board states to predict the next set of valid moves.

/**
 * 
 * use 'ulimit -c unlimited' to make core dump
 * 
 * TODO add ability to make checkpoints to save progress
 *  - Interpret Ctrl+C interrupt signal and cause a save
 * 
 * [DONE] make a way to swap out memory from disk when it's not used.
 * 
 */

// TODO Re-work file system to work with heir.h
// TODO Re-make hash to do counter-clockwise spiral from center of board
// TODO Maybe try to use heroseh's gui stuff to make interface look nice


void display_board(board b) {
    if(b) {
        for(uint8_t r = 0; r < b->height; r++) {
            for(uint8_t c = 0; c < b->width; c++) {
                printf("%c", board_get(b, r, c) + '0');
            }
            printf("\n");
        }
        printf("\n");
    }
}


void display_capture_counts(uint64_t cc) {
    /*
    * 0: upper-left
    * 1: up
    * 2: upper-right
    * 3: left
    * 4: right
    * 5: lower-left
    * 6: lower
    * 7: lower-right
    */
    printf("Capture Counts:\n");
    uint8_t c;
    for(uint8_t i = 0; i < 8; i++) {
        c = capture_count_get_count(cc, i);
        switch(i) {
            case 0:
                printf("\tNorthwest: ");
                break;
            case 1:
                printf("\tNorth: ");
                break;
            case 2:
                printf("\tNortheast: ");
                break;
            case 3:
                printf("\tWest: ");
                break;
            case 4:
                printf("\tEast: ");
                break;
            case 5:
                printf("\tSouthwest: ");
                break;
            case 6:
                printf("\tSouth: ");
                break;
            case 7:
                printf("\tSoutheast: ");
                break;
        }
        printf("%u\n", c);
    }
    printf("\n");
}


int main() {
    char d;
    while(1) {
        printf("Would you like to restore from a checkpoint?(y/N): ");
        d = getc(stdin);
        if(d == '\n' || d == 'n' || d == 'N') {
            d = 'n';
            break;
        }
        else if(d == 'y' || d == 'Y') {
            d = 'y';
            break;
        }
    }

    getc(stdin);    // Read the extra \n character

    // Allocate all of the stack parameters necessary for file restoration
    heirarchy cache;
    uint64_t count = 0, explored_count = 1;
    char* checkpoint_filename;

    // Calculate the number of processors to use
    uint32_t procs = get_nprocs();
    #ifndef limitprocs
        procs = procs << 1;
    #else
        procs = 12;
    #endif

    // Setup the locks
    pthread_mutex_t counter_lock, explored_lock, file_lock;

    // Setup the checkpoint saving system
    FILE** checkpoint_file = malloc(sizeof(FILE*));
    if(!checkpoint_file) err(1, "Memory error while allocating checkpoint file pointer\n");
    uint64_t saving_counter;

    // Initialize the locks
    if(pthread_mutex_init(&counter_lock, 0) || pthread_mutex_init(&explored_lock, 0) || 
       pthread_mutex_init(&file_lock, 0) || pthread_mutex_init(&saving_lock, 0)) 
        err(4, "Initialization of counter mutex failed\n");

    #pragma region Round nprocs to the correct number
    board b = create_board(1, 6, 6);
    
    // Setup the queue
    ptr_arraylist search_queue = create_ptr_arraylist(procs + 1);

    // Account for reflections and symmetry by using 1 of the 4 possible starting moves
    coord* next_moves = find_next_boards(b);

    for(char im = 0; next_moves[im]; im++) {
        coord m = next_moves[im];
        uint16_t sm = coord_to_short(m);
        board cb = clone_board(b);
        board_place_piece(cb, m->row, m->column);
        append_pal(search_queue, cb);
        free(m);
        break;
    }

    for(char im = 1; next_moves[im]; im++) {
        coord m = next_moves[im];
        free(m);
    }

    free(next_moves);
    destroy_board(b);

    // Perform the BFS
    while(search_queue->pointer < procs) {
        b = pop_front_pal(search_queue);
        next_moves = find_next_boards(b);

        for(char im = 0; next_moves[im]; im++) {
            coord m = next_moves[im];
            uint16_t sm = coord_to_short(m);
            board cb = clone_board(b);
            board_place_piece(cb, m->row, m->column);
            append_pal(search_queue, cb);
            free(m);
        }

        explored_count++;

        free(next_moves);
        destroy_board(b);
    }

    procs = search_queue->pointer;

    printf("Rounded nprocs to %d threads\n", procs);
    #pragma endregion

    ptr_arraylist threads;

    if(d == 'y') {
        char** restore_filename = malloc(sizeof(char*));
        printf("Please enter a file to restore from: ");
        scanf("%ms", restore_filename);
        getc(stdin);    // Read the extra \n character
        processed_file pf = restore_progress_v2(*restore_filename);

        while(1) {
            printf("Would you like to continue saving to this checkpoint?(y/N): ");
            d = getc(stdin);
            // printf("%c\n", d);
            if(d == '\n' || d == 'n' || d == 'N') {
                checkpoint_filename = find_temp_filename("checkpoint.bin\0");
                break;
            }
            else if(d == 'y' || d == 'Y') {
                checkpoint_filename = malloc(sizeof(char) * strlen(*restore_filename));
                strcpy(checkpoint_filename, *restore_filename);
                break;
            }
            else printf("\n");
        }

        free(*restore_filename);

        // De-allocate the stuff we did to round procs, because we don't need it now.
        while(search_queue->pointer) destroy_board(pop_front_pal(search_queue));
        destroy_ptr_arraylist(search_queue);

        // Begin restore
        count = pf->found_counter;
        explored_count = pf->explored_counter;

        ptr_arraylist stacks = pf->processor_stacks;
        if(procs != pf->num_processors) {
            printf("Redistributing workload to match core count\n");
            // Redistribute workload
            ptr_arraylist all_current_boards = create_ptr_arraylist(procs * 1000), important_boards = create_ptr_arraylist(pf->num_processors + 1);
            for(ptr_arraylist* p = (ptr_arraylist*)pf->processor_stacks->data; *p; p++) {
                for(uint64_t pb = 0; pb < (*p)->pointer; pb++) {
                    board b = (*p)->data[pb];
                    // printf("Collected\n"); display_board(b);
                    append_pal((pb) ? all_current_boards : important_boards, b);
                }
                destroy_ptr_arraylist(*p);
            }

            while(stacks->pointer) pop_back_pal(stacks);

            // printf("Distributing %lu boards\n", all_current_boards->pointer + important_boards->pointer);
            
            for(uint64_t p = 0; p < procs; p++) append_pal(stacks, create_ptr_arraylist(1000));

            uint64_t p_ptr = 0;

            while(important_boards->pointer) {
                append_pal(stacks->data[p_ptr++], pop_back_pal(important_boards));
                if(p_ptr == procs) p_ptr = 0;
            }

            while(all_current_boards->pointer) {
                append_pal(stacks->data[p_ptr++], pop_back_pal(all_current_boards));
                if(p_ptr == procs) p_ptr = 0;
            }
        }

        // Create threads

        // Distribute the initial states to a set of new pthreads.
        threads = create_ptr_arraylist(procs + 1);

        for(uint64_t t = 0; t < procs; t++) {
            pthread_t* thread_id = (pthread_t*)malloc(sizeof(pthread_t));
            if(!thread_id) err(1, "Memory error while allocating thread id\n");

            #ifdef filedebug
                printf("%p %s with %lu elements\n", stacks->data[t], 
                                                    (((ptr_arraylist)(stacks->data[t]))) ? "Valid" : "Not Valid",
                                                    ((ptr_arraylist)(stacks->data[t]))->pointer);
            #endif

            processor_args args = create_processor_args(t, stacks->data[t], pf->cache, 
                                                        &count, &counter_lock,
                                                        &explored_count, &explored_lock,
                                                        &saving_counter, checkpoint_file, &file_lock);

            // walker_processor(args);
            pthread_create(thread_id, 0, walker_processor_pre_stacked, (void*)args);
            append_pal(threads, thread_id);
        }

        cache = pf->cache;
    }
    else {
        // #ifdef smallcache
        //     cache = create_hashtable(10, &board_hash);
        // #else
        //     cache = create_hashtable(1000000, &board_hash);
        // #endif
        cache = create_heirarchy();

        checkpoint_filename = find_temp_filename("checkpoint.bin\0");

        // Distribute the initial states to a set of new pthreads.
        threads = create_ptr_arraylist(procs + 1);

        for(uint64_t t = 0; t < procs; t++) {
            pthread_t* thread_id = (pthread_t*)malloc(sizeof(pthread_t));
            if(!thread_id) err(1, "Memory error while allocating thread id\n");

            processor_args args = create_processor_args(t, search_queue->data[t], cache, 
                                                        &count, &counter_lock,
                                                        &explored_count, &explored_lock,
                                                        &saving_counter, checkpoint_file, &file_lock);

            // walker_processor(args);
            pthread_create(thread_id, 0, walker_processor, (void*)args);
            append_pal(threads, thread_id);
        }
    }

    printf("Starting walk...\n");
    printf("Running on %d threads\n", procs);
    printf("Saving checkpoints to %s\n", checkpoint_filename);

    // for(uint64_t t = 0; t < threads->pointer; t++) pthread_join(*(pthread_t*)(threads->data[0]), 0);
    time_t start = time(0), current, save_timer = time(0), fps_timer = time(0);
    clock_t cstart = clock();
    uint32_t cpu_time, cpu_days, cpu_hours, cpu_minutes, cpu_seconds,
             run_time, run_days, run_hours, run_minutes, run_seconds, save_time, previous_run_time = start, fps_update_time;
    uint64_t previous_board_count = 0, fps = 0;
    while(1) {
        current = time(0);
        run_time = current - start;
        save_time = (current - save_timer) / 3600;
        fps_update_time = (current - fps_timer) / 1;

        run_days = run_time / 86400;
        run_hours = (run_time / 3600) % 24;
        run_minutes = (run_time / 60) % 60;
        run_seconds = run_time % 60;

        cpu_time = (uint32_t)(((double)(clock() - cstart)) / CLOCKS_PER_SEC);
        cpu_days = cpu_time / 86400;
        cpu_hours = (cpu_time / 3600) % 24;
        cpu_minutes = (cpu_time / 60) % 60;
        cpu_seconds = cpu_time % 60;

        if(fps_update_time) {
            fps = (explored_count - previous_board_count);
            previous_board_count = explored_count;
            fps_timer = time(0);
        }

        printf("\rFound %ld final board states. Explored %ld boards @ %ld boards/sec. Runtime: %0d:%02d:%02d:%02d CPU Time: %0d:%02d:%02d:%02d %s", 
               count, explored_count, fps,
               run_days, run_hours, run_minutes, run_seconds,
               cpu_days, cpu_hours, cpu_minutes, cpu_seconds,
               (save_time) ? "Saving..." : "");
        fflush(stdout);

        if(save_time) {
            save_progress_v2(checkpoint_file, &file_lock, checkpoint_filename, &saving_counter, cache, count, explored_count, procs);
            save_timer = time(0);
        }
        sched_yield();
    }

    printf("\nThere are %ld possible board states\n", count);
}
