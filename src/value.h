//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_VALUE_H
#define CTOK_VALUE_H

#include <string.h>

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
/**
 * If a double has all of its NaN bits set, and the quiet NaN bit is set, and one more for good measure - we can be pretty
 * certain it is one of the bit patterns we ourselves have set aside for other types. To check that, we mask out all of the
 * bits except for our set of NaN bits. If all of those bits are set, it must be a NaN-boxed value of some other Tok type.
 * Otherwise, it's actually a number.
 */
#define QNAN     ((uint64_t)0x7ffc000000000000)

/**
 * We use two (provides total 4 unique values) of the lowest bits of the mantissa space as a "type tag" to determine which
 * of the three singleton values (Nil, false, true) we're looking at.
 */
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

// When NaN Boxing is enabled, the actual type of a Value is a flat, unsigned 64-bit integer.
typedef uint64_t Value;

// Macros to type check a Tok Value
/// Macro to make check if a value is a pure Tok Boolean.
#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL)
#define IS_NIL(value)       ((value) == NIL_VAL)
// Every Value that is not a number will use a special quiet NaN representation.
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

/// Macro to unwrap a Tok Value to a C bool.
#define AS_BOOL(value)      ((value) == TRUE_VAL)
/// Macro to unwrap a Tok Value to a double.
#define AS_NUMBER(value)    valueToNum(value)
/// Macro to unwrap the Obj pointer out of the Tok Value.
#define AS_OBJ(value) ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))


/// Macro to convert a C bool into a Tok Boolean.
#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
/// Macro to define a <code>false</code> Tok Value.
#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
/// Macro to define a <code>true</code> Tok Value.
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
/// Macro to define a NIL_VAL, simply bitwise OR the quiet NaN bits and the type tag.
#define NIL_VAL     ((Value)(uint64_t)(QNAN | TAG_NIL))
/// Macro to pun a number to a Tok Value
#define NUMBER_VAL(num) numToValue(num)
/// Macro to convert an object pointer to a Tok Value.
#define OBJ_VAL(obj) (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

/**
 * Function to convert a Tok Value to a double.
 * @param value Tok Value to be converted.
 * @return double representation of the Tok Value.
 */
static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

/**
 * Function to convert a double into a Tok Value.\n
 * Creates a local variable, copies over the bytes into the Tok Value representation.
 * @param num number to be converted.
 * @return Tok Value representation of the Double.
 */
static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#else

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

#endif

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
