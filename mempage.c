#include "mempage.h"
#include "arraylist.h"

#include <stdlib.h>
#include <err.h>

mempage create_mempage(size_t page_max, __uint128_t num_bins, size_t bin_size) {
    mempage mp = malloc(sizeof(mempage_str));
    if(!mp) err(1, "Memory Error while allocating memory page manager\n");

    __uint128_t pages = (num_bins / page_max) + 1;

    #ifdef mempagedebug
        printf("Creating a mempage with %lu %lu pages\n", ((uint64_t*)&pages)[1], ((uint64_t*)&pages)[0]);
    #endif

    // mp->pages = create_ll();
    mp->pages = malloc(sizeof(__uint128_t**) * pages);
    if(!mp->pages) err(1, "Memory error while allocating book for mempage\n");

    mp->bin_counts = malloc(sizeof(size_t**) * pages);
    if(!mp->bin_counts) err(1, "Memory error while allocating book for mempage\n");

    mp->page_count = pages;
    mp->count_per_page = page_max;
    mp->num_bins = num_bins;
    mp->bin_size = bin_size;

    for(__uint128_t p = 0; p < pages; p++) {
        __uint128_t** bins = malloc(sizeof(__uint128_t*) * page_max);
        if(!bins) err(1, "Memory error while allocating array for mempage page\n");

        size_t** sizes = malloc(sizeof(size_t*) * page_max);
        if(!sizes) err(1, "Memory error while allocating array for mempage page\n");

        for(size_t b = 0; b < bin_size; b++) {
            bins[b] = calloc(bin_size, sizeof(__uint128_t));
            if(!bins[b]) err(1, "Memory error while allocating bin for mempage\n");

            sizes[b] = mp->bin_size;
        }

        mp->pages[p] = bins;
        mp->bin_counts[p] = sizes;
    }

    return mp;
}

void destroy_mempage(mempage mp) {
    if(mp) {
        for(__uint128_t p = 0; p < mp->page_count; p++) {
            for(size_t b = 0; b < mp->bin_size; b++) free(mp->pages[p][b]);
            free(mp->pages[p]);
            free(mp->bin_counts[p]);
        }
        free(mp->pages);
        free(mp->bin_counts);
        free(mp);
    }
}

// __uint128_t* mempage_get(mempage mp, __uint128_t index) {
//     if(index >= mp->num_bins) err(4, "Index out of bounds in mempage\n");

//     uint32_t page = index / mp->count_per_page, page_index = index % mp->count_per_page;

//     // Extract the page
//     __uint128_t** l = mp->pages[page];

//     // Extract the int
//     return l[page_index];
// }

// void mempage_put(mempage mp, __uint128_t index, void* data) {
//     if(index >= mp->num_bins) err(4, "Index out of bounds in mempage\n");
    
//     uint32_t page = index / mp->count_per_page, page_index = index % mp->count_per_page;

//     // Extract the page
//     ptr_arraylist l = (ptr_arraylist)mp->pages->data[page];

//     // Extract the int
//     (l)->data[page_index] = data;
// }

void mempage_append_bin(mempage mp, __uint128_t bin_index, __uint128_t value) {
    if(bin_index >= mp->num_bins) err(4, "Index out of bounds in mempage\n");
    uint32_t page = bin_index / mp->count_per_page, page_index = bin_index % mp->count_per_page;
    __uint128_t** l = mp->pages[page];
    size_t bcount = mp->bin_counts[page][page_index];
    
    __uint128_t* bin = l[page_index];
    for(size_t iter = 0;; iter++) {
        if(!bin[iter]) {
            bin[iter] = value;
            break;
        }
        else if(iter == bcount) {
            // reallocate the bin
            #ifdef mempagedebug
                printf("Reallocating bin %lu %lu to %ld\n", ((uint64_t*)&bin_index)[1], ((uint64_t*)&bin_index)[0], bcount + 10);
            #endif

            bin = realloc(bin, sizeof(__uint128_t) * (bcount + 10));
            bcount += 10;

            l[page_index] = bin;
            mp->bin_counts[page][page_index] = bcount;
        }
    }
}

uint8_t mempage_value_in_bin(mempage mp, __uint128_t bin_index, __uint128_t value) {
    if(bin_index >= mp->num_bins) err(4, "Index out of bounds in mempage\n");
    uint32_t page = bin_index / mp->count_per_page, page_index = bin_index % mp->count_per_page;
    __uint128_t** l = mp->pages[page];
    size_t bcount = mp->bin_counts[page][page_index];
    
    __uint128_t* bin = l[page_index];
    for(size_t b = 0; b < bcount; b++) if(bin[b] == value) return 1;
    return 0;
}

void mempage_clear_all(mempage mp) {
    for(__uint128_t p = 0; p < mp->page_count; p++) {
        for(size_t b = 0; b < mp->bin_size; b++) {
            free(mp->pages[p][b]);
            mp->pages[p][b] = calloc(mp->bin_size, sizeof(__uint128_t));
            if(!mp->pages[p][b]) err(1, "Memory error while allocating bin for mempage\n");
            mp->bin_counts[p][b] = mp->bin_size;
        }
    }
}

void mempage_realloc(mempage mp, __uint128_t bin_count) {
    __uint128_t pages = (bin_count / mp->count_per_page) + 1;
    __uint128_t diff = pages - mp->page_count;

    #ifdef mempagedebug
        printf("Creating a mempage with %lu %lu pages\n", ((uint64_t*)&pages)[1], ((uint64_t*)&pages)[0]);
    #endif

    if(diff) {
        // mp->pages = create_ll();
        mp->pages = realloc(mp->pages, sizeof(__uint128_t**) * pages);
        if(!mp->pages) err(1, "Memory error while allocating book for mempage\n");

        mp->bin_counts = realloc(mp->bin_counts, sizeof(size_t**) * pages);
        if(!mp->bin_counts) err(1, "Memory error while allocating book for mempage\n");

        for(size_t p = mp->page_count; p < pages; p++) {
            __uint128_t** bins = malloc(sizeof(__uint128_t*) * mp->count_per_page);
            if(!bins) err(1, "Memory error while allocating array for mempage page\n");

            size_t** sizes = malloc(sizeof(size_t*) * mp->count_per_page);
            if(!sizes) err(1, "Memory error while allocating array for mempage page\n");

            for(size_t b = 0; b < mp->bin_size; b++) {
                bins[b] = calloc(mp->bin_size, sizeof(__uint128_t));
                if(!bins[b]) err(1, "Memory error while allocating bin for mempage\n");

                sizes[b] = mp->bin_size;
            }

            mp->pages[p] = bins;
            mp->bin_counts[p] = sizes;
        }

        mp->page_count = pages;
    }
}