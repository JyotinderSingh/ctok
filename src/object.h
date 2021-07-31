//
// Created by Jyotinder Singh on 27/06/21.
//

#ifndef CTOK_OBJECT_H
#define CTOK_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "value.h"

/**
 * Since the union in the value (of type 'Value') in this context points to an Obj stored on the heap,
 * we use this macro to extract the type information from that instance to see what kind of object it is.
 */
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

/**
 * Macros that help us make sure if it's safe to case a value into a specific object type.
 */
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)    isObjType(value, OBJ_STRING)

/**
 * Macros to cast an Obj value into a specific Object type.
 */
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)    (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
/**
 * Enums that define all the different kinds of objects supported by Tok.
 */
typedef enum {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

/**
 * This struct acts sort of like a base class for the different kinds of objects that are supported in Tok.
 */
struct Obj {
    ObjType type;
    struct Obj* next;
};

/**
 * Functions are first class in Tok. Hence they are actual Tok objects.
 */
typedef struct {
    Obj obj;
    // number of parameters a function expects.
    int arity;
    int upvalueCount;
    Chunk chunk;
    // we store the function name as well, handy for reporting errors.
    ObjString* name;
} ObjFunction;

/**
 * Defines NativeFn as a type, which takes two arguments(int, Value*) and returns a Value.
 * NativeFn takes an argument count, and a pointer to the first argument on the stack.
 * It accesses the arguments through this pointer. Once done, it returns the result value.
 */
typedef Value (* NativeFn)(int argCount, Value* args);

/**
 * Struct to handle native functions
 */
typedef struct {
    Obj obj;
    // Pointer to a C function that implements the native behaviour.
    NativeFn function;
} ObjNative;

/**
 * Defines a String object, that internally makes use of the Obj struct to define object related information.
 * A string object contains an array of characters. Those are stored in a separate, heap allocated array.
 * We also store the number of bytes in the array - it isn't strictly necessary but it lets us tell how much memory is
 * allocated for the string without walking the array to find the null terminator.
 * Also caches the hash for the string, helps in optimizing hash table lookups. Since strings are immutable in Tok,
 * we don't need to worry about cache invalidation.
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
    uint32_t hash;
};

/**
 * Runtime representation of an Upvalue.
 */
typedef struct ObjUpvalue {
    Obj obj;
    // field to point to the closed-over variable. This is a pointer to a Value and not the Value itself.
    // This allows effects of operations on the Value to be visible outside the executing function as well.
    Value* location;
    // field where the value of the upvalue will live once it is closed. (The original reference goes out of scope)
    Value closed;
    // pointer to the next upvalue in the list of upvalues.
    struct ObjUpvalue* next;
} ObjUpvalue;

/**
 * Every ObjFunction is wrapped in an ObjClosure, even if the function doesn't actually close over and capture any surrounding
 * local variables.
 */
typedef struct {
    Obj obj;
    ObjFunction* function;
    // pointer to a dynamically allocated array of pointers to upvalues. We also store the number of elements in the array.
    ObjUpvalue** upvalues;
    // number of elements in the upvalues array.
    int upvalueCount;
} ObjClosure;

ObjClosure* newClosure(ObjFunction* function);

ObjFunction* newFunction();

ObjNative* newNative(NativeFn function);

ObjString* takeString(char* chars, int length);

ObjString* copyString(const char* chars, int length);

ObjUpvalue* newUpvalue(Value* slot);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif //CTOK_OBJECT_H
