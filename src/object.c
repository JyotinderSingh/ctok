//
// Created by Jyotinder Singh on 27/06/21.
//

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// This macro is used as a wrapper to help get the pointer back in the required Object type.
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

/**
 * Helper function that allocates an object of the given size on the heap. The size is not just the size of the Obj itself.
 * Teh caller passes in the number of bytes so that there is room for the extra payload fields needed by the specific
 * object type being created.
 * It then initializes the Obj state.
 * @param size
 * @param type
 * @return
 */
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*) reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;
    return object;
}

/**
 * Performs the heavy lifting for defining a new string. It acts like a constructor in an OOP language.
 * @param chars
 * @param length
 * @return
 */
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    tableSet(&vm.strings, string, NIL_VAL);
    return string;
}

/**
 * Utility function to hash a given string using FNV-1a algorithm.
 */
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t) key[i];
        hash *= 16777619;
    }
    return hash;
}

/**
 * Utility function to allocate a ObjString in the heap, given a char array and it's length.
 * This function takes ownership of the string and passes it into the heap. Because we've already dynamically allocated
 * a character array on the heap, and making a copy of it would be redundant.
 * @param chars
 * @param length
 * @return
 */
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // String interning.
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) {
        // If we found this string in the table, it means we need to free the newly allocated memory for it (which was passed to this function) - since,
        // we would be using the interned reference to it anyway.
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

/**
 * Utility function to copy a string in the source code to the heap, and return a reference to it.
 * This function assumes that it cannot take ownership of the characters you pass in. Instead, it conservatively creates
 * a copy of the characters on the heap that the ObjString can own.
 * This is the right approach for string literals where the passed-in characters are in the middle of the source string.
 * @param chars character array pointing to the string.
 * @param length length of the string.
 * @return ObjString* pointing to a newly allocated string object on the heap.
 */
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // String interning.
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    // First we allocate a new array on the heap, just big enough for the string's characters and the trailing terminator
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

/**
 * Utility function to print the value of an Object.
 * @param value
 */
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}