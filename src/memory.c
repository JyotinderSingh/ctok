//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdlib.h>

#include "memory.h"
#include "vm.h"

/**
 * reallocate is a single function we use for all the dynamic memory management in ctok -
 * allocating memory, freeing memory, and changing the size of the existing allocation.
 * @param pointer
 * @param oldSize
 * @param newSize
 * @return
 */
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

/**
 * Utility function to free up particular objects in the memory given their references.
 * @param object
 */
static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*) object;
            // Free the chunk present inside the function first.
            freeChunk(&function->chunk);
            // Free the function object itself.
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_STRING: {
            ObjString* string = (ObjString*) object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
    }
}

/**
 * Function to help cleanup memory references after the VM has completed its operations.
 */
void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}