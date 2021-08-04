//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_MEMORY_H
#define CTOK_MEMORY_H

#include "common.h"
#include "object.h"

/**
 * Macro to allocate an array with a given element type and count.
 */
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

/**
 * Macro to free an Obj's allocated memory on the heap.
 */
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

/**
 * Macro that calculates the new capacity based on a given current capacity.
 */
#define GROW_CAPACITY(capacity) \
        ((capacity) < 8 ? 8 : (capacity) * 2)

/**
 * Macro to help create/grow an array to the required size.
 */
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
        (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

/**
 * Macro used to clear an array and free up the memory.
 */
#define FREE_ARRAY(type, pointer, oldCount) \
        reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

void markObject(Obj* object);

void markValue(Value value);

void freeObjects();

// Pull yourself together,
// you piece of trash
void collectGarbage();

#endif //CTOK_MEMORY_H
