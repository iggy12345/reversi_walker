#include "hashtable.h"
#include "arraylist.h"

#include <stdlib.h>
#include <err.h>
#include <assert.h>

/**
 * @brief Create a hashtable object
 * 
 * @param initial_bin_count The number of bins to start off with
 * @param hash The hash function for hashing the data into the hashtable
 * @return hashtable 
 */
hashtable create_hashtable(uint64_t initial_bin_count, __uint128_t (*hash)(void*)) {
    assert(initial_bin_count);
    hashtable t = malloc(sizeof(hashtable_str));
    if(!t) err(1, "Memory Error while trying to allocate hashtable\n");
    t->bins = create_mempage(500000000, initial_bin_count); // calloc(initial_bin_count, sizeof(uint128_arraylist));
    for(uint64_t i = 0; i < initial_bin_count; i++) mempage_put(t->bins, i, create_uint128_arraylist(65));
    t->bin_count = initial_bin_count;
    t->size = 0;
    t->hash = hash;
    return t;
}

void clear_bins(hashtable t) {
    for(__uint128_t l = 0; l < t->bin_count; l++) destroy_uint128_arraylist((uint128_arraylist)mempage_get(t->bins, l));
    destroy_mempage(t->bins);
}

/**
 * @brief Destroys the hashtable object
 * 
 * @param t 
 */
void destroy_hashtable(hashtable t) {
    if(t) {
        clear_bins(t);
        free(t);
    }
}

/**
 * @brief Get all of the pairs of keys and values from the hashtable
 * 
 * @param t The hashtable to extract the pairs from
 * @return __uint128_t* An array of keys that must be free'd by the user.
 */
__uint128_t* get_pairs(hashtable t) {
    if(t) {
        uint128_arraylist result = create_uint128_arraylist(t->size + 1);
        for(uint64_t i = 0; i < t->bin_count; i++) {
            uint128_arraylist alist = (uint128_arraylist)mempage_get(t->bins, i);
            __uint128_t* arr = alist->data;
            for(uint64_t j = 0; j < alist->pointer; j++) append_ddal(result, arr[j]);
            // free(arr);
        }

        __uint128_t* resultarr = result->data;
        free(result);
        return resultarr;
    }
}

/**
 * @brief Inserts a value into the hash table
 * 
 * @param t The table to insert the value into
 * @param value The value to insert
 * @return uint64_t Returns the value of the key that value hashed to
 */
__uint128_t put_hs(hashtable t, void* value) {
    if(t) {
        __uint128_t k = t->hash(value);
        if(!k) err(2, "Hit a hash value that is 0\n");

        append_ddal((uint128_arraylist)mempage_get(t->bins, k % t->bin_count), k);
        
        if(++t->size > (t->bin_count << 15)) {
            //re-hash
            __uint128_t* pairs = get_pairs(t);

            clear_bins(t);

            t->bins = create_mempage(500000000, t->size + 64);
            // if(!t->bins) err(1, "Memory Error while re allocating bins for hashtable\n");
            for(uint64_t i = t->size; i < t->size + 64; i++) mempage_put(t->bins, i, create_uint128_arraylist(65));
            t->bin_count = t->size + 64;

            for(__uint128_t* p = pairs; *p; p++) append_ddal((uint128_arraylist)mempage_get(t->bins, *p % t->bin_count), *p);
        }

        return k;
    }

    return 0;
}

/**
 * @brief Checks if the given value exists in the hashtable
 * 
 * @param t The hashtable to check
 * @param value The value to check for
 * @return uint8_t Returns 1 if the value exists, and 0 otherwise
 */
uint8_t exists_hs(hashtable t, void* value) {
    if(t && t->size) {
        __uint128_t key = t->hash(value);
        if(!key) err(2, "Hit a hash value that is 0\n");

        uint128_arraylist bin = mempage_get(t->bins, key % t->bin_count);
        for(__uint128_t* n = bin->data; *n; n++) {
            __uint128_t p = *n;
            if(p == key) return 1;
        }
    }
    return 0;
}