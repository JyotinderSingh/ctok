//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_VALUE_H
#define CTOK_VALUE_H

#include "common.h"

typedef double Value;

/**
 * Struct to store and manage constant values inside a chunk in the form of an array).
 */
typedef struct {
    int capacity;
    int count;
    Value *values;
} ValueArray;

/**
 * Initialize a ValueArray to make it ready for use.
 * @param array
 */
void initValueArray(ValueArray *array);

/**
 * Function to write a constant to a the ValueArray.
 * @param array
 * @param value
 */
void writeValueArray(ValueArray *array, Value value);

/**
 * Function to un-initialize a ValueArray.
 * @param array
 */
void freeValueArray(ValueArray *array);

/**
 * Function to print a Value object.
 * @param value
 */
void printValue(Value value);

#endif //CTOK_VALUE_H
