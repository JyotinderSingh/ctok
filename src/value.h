//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_VALUE_H
#define CTOK_VALUE_H

#include "common.h"

typedef double Value;

typedef struct {
    int capacity;
    int count;
    Value *values;
} ValueArray;

void initValueArray(ValueArray *array);

void writeValueArray(ValueArray *array, Value value);

void freeValueArray(ValueArray *array);

void printValue(Value value);

#endif //CTOK_VALUE_H
