//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_VALUE_H
#define CTOK_VALUE_H

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

/**
 * Struct to store our different kinds of data types.
 * We maintain one field called type to track what kind of variable is currently stored in the memory.
 * We also use a union to store the actual bytes of the data in an efficient and overlapping manner.
 * We use 'as' for the name of the union field since it reads nicely, almost like a cast when we pul
 * the various values out.
 * The Obj type contains a pointer to the corresponding object stored at that location in the heap.
 */
typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

/**
 * The following macros help us store/access different kinds of values in the Value struct.
 * AS_BOOL and AS_NUMBER need to be given the right kinds of values to unpack, otherwise bad things will happen.
 */
#define IS_BOOL(value)      ((value).type == VAL_BOOL)
#define IS_NIL(value)       ((value).type == VAL_NIL)
#define IS_NUMBER(value)    ((value).type == VAL_NUMBER)
#define IS_OBJ(value)       ((value).type == VAL_OBJ)

#define AS_OBJ(value)      ((value).as.obj)
#define AS_BOOL(value)      ((value).as.boolean)
#define AS_NUMBER(value)    ((value).as.number)

#define BOOL_VAL(value)     ((Value){VAL_BOOL, {.boolean = (value)}})
#define NIL_VAL             ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)   ((Value){VAL_NUMBER, {.number = (value)}})
#define OBJ_VAL(object)      ((Value){VAL_OBJ, {.obj = (Obj*)(object)}})

/**
 * Struct to store and manage constant values inside a chunk in the form of an array).
 */
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

/**
 * Utility function to check if two values are equal.
 * @param a
 * @param b
 * @return
 */
bool valuesEqual(Value a, Value b);

/**
 * Initialize a ValueArray to make it ready for use.
 * @param array
 */
void initValueArray(ValueArray* array);

/**
 * Function to write a constant to a the ValueArray.
 * @param array
 * @param value
 */
void writeValueArray(ValueArray* array, Value value);

/**
 * Function to un-initialize a ValueArray.
 * @param array
 */
void freeValueArray(ValueArray* array);

/**
 * Function to print a Value object.
 * @param value
 */
void printValue(Value value);

#endif //CTOK_VALUE_H
