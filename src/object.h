//
// Created by Jyotinder Singh on 27/06/21.
//

#ifndef CTOK_OBJECT_H
#define CTOK_OBJECT_H

#include "common.h"
#include "value.h"

/**
 * Since the union in the value (of type 'Value') in this context points to an Obj stored on the heap,
 * we use this macro to extract the type information from that instance to see what kind of object it is.
 */
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

/**
 * Macros that help us make sure if it's safe to case a value into a specific object type.
 */
#define IS_STRING(value)    isObjType(value, OBJ_STRING)

/**
 * Macros to cast an Obj value into a specific Object type.
 */
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
/**
 * Enums that define all the different kinds of objects supported by Tok.
 */
typedef enum {
    OBJ_STRING
} ObjType;

/**
 * This struct acts sort of like a base class for the different kinds of objects that are supported in Tok.
 */
struct Obj {
    ObjType type;
    struct Obj* next;
};

/**
 * Defines a String object, that internally makes use of the Obj struct to define object related information.
 * A string object contains an array of characters. Those are stored in a separate, heap allocated array.
 * We also store the number of bytes in the array - it isn't strictly necessary but it lets us tell how much memory is
 * allocated for the string without walking the array to find the null terminator.
 *
 * Design note:
 * Because ObjString is an Obj, it also needs the state all Objs share. It accomplishes that by having the first
 * field as Obj. C specifies that struct fields are arranged in memory in the order that they are declared.
 * This is designed to enable a clever pattern: You can take a pointer to a struct and safely convert it into a pointer
 * to its first field and back. Given an ObjString*, you can safely cast it to an Obj* and then access the 'type' field
 * from it. Every ObjString 'is' an Obj in the OOP sense.
 */
struct ObjString {
    Obj obj;
    int length;
    char* chars;
};

ObjString* takeString(char* chars, int length);

ObjString* copyString(const char* chars, int length);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif //CTOK_OBJECT_H
