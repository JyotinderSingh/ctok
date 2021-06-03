//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdio.h>

#include "memory.h"
#include "value.h"

/**
 *
 * @param array
 */
void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

/**
 * Function to insert a value into the ValueArray's values field.
 * @param array
 * @param value
 */
void writeValueArray(ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

/**
 * Function to free up memory from the ValueArray and re-initialize into a stable state.
 * @param array
 */
void freeValueArray(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

/**
 * Function to print the value of a Value object.
 * @param value
 */
void printValue(Value value) {
    printf("%g", value);
}