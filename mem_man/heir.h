#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "mmap_man.h"

typedef struct __heirarchy_str {
    void** first_level;
    mmap_man final_level;
    size_t num_bits_per_level;
    size_t num_levels;
    size_t page_size;
} heirarchy_str;

typedef heirarchy_str* heirarchy;

heirarchy create_heirarchy();
void destroy_heirarchy(heirarchy h);

uint8_t heirarchy_insert(heirarchy h, __uint128_t key);

void to_file_heir(FILE* fp, heirarchy h);

heirarchy from_file_heir(FILE* fp);
