//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_MEMORY_H
#define CTOK_MEMORY_H

#include "common.h"

/**
 * This macro calculates the new capacity based ona given current capacity.
 */
#define GROW_CAPACITY(capacity) \
        ((capacity) < 8 ? 8 : (capacity) * 2)

/**
 * This macro can help us create/grow an array to the required size.
 */
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
        (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

/**
 * This macro is used to clear an array and free up the memory.
 */
#define FREE_ARRAY(type, pointer, oldCount) \
        reallocate(pointer, sizeof(type) * (oldCount), 0)

void *reallocate(void *pointer, size_t oldSize, size_t newSize);

#endif //CTOK_MEMORY_H
