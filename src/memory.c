//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdlib.h>

#include "memory.h"

/**
 * reallocate is a single function we use for all the dynamic memory management in ctok -
 * allocating memory, freeing memory, and changing the size of the existing allocation.
 * @param pointer
 * @param oldSize
 * @param newSize
 * @return
 */
void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}