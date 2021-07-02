//
// Created by Jyotinder Singh on 02/06/21.
//

#ifndef CTOK_VM_H
#define CTOK_VM_H

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
    Chunk* chunk;
    /**
    * we use a C pointer to keep track of where we are in the bytecode array (which instruction we need to execute next)
    * since it's faster than using an index to access something in an array.
    */
    uint8_t* ip;    // instruction pointer.
    Value stack[STACK_MAX];
    Value* stackTop;
    Table strings;
    Obj* objects;
} VM;

/**
 *  The VM runs a chunk and responds with one of the values from this enum.
 */
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();

void freeVM();

InterpretResult interpret(const char* source);

void push(Value value);

Value pop();

#endif //CTOK_VM_H
