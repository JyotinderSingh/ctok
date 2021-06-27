//
// Created by Jyotinder Singh on 27/06/21.
//

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

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
static ObjString* allocateString(char* chars, int length) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    return string;
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
    return allocateString(chars, length);
}

/**
 * Utility function to copy a string in the source code to the heap, and return a reference to it.
 * This function assumes that it cannot take ownership of the characters you pass in. Instead, it conservatively creates
 * a copy of the characters on the heap that the ObjString can own.
 * This is the right approach for string literals where the passed-in characters are in the middle of the source string.
 * @param chars
 * @param length
 * @return
 */
ObjString* copyString(const char* chars, int length) {
    // First we allocate a new array on the heap, just big enough for the string's characters and the trailing terminator
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length);
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