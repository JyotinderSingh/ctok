//
// Created by Jyotinder Singh on 02/06/21.
//

#ifndef CTOK_VM_H
#define CTOK_VM_H

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

/**
 * A CallFrame represents a single on-going function call.
 * The <code>slots</code> field points into the VM's value stack at the first slot that this function can use.
 * Each frame will store it's own <code>ip</code>. When we return from a function, the VM will jump to the <code>ip</code>
 * of the caller's CallFrame and resume from there.
 * The <code>closure</code> pointer points to the closure of the function being called.
 * We use that to look up constants, and a few other things. Each time a function is called, we create one of these structs.
 */
typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

/**
 * Struct for managing the VM instance.
 * <code>frames</code> is an array for storing the CallFrames for function calls.
 * <code>frameCount</code> stores the current height of the CallFrame stack.
 * <code>stack</code> contains all the runtime values for the VM.
 * <code>stackTop</code> stores the height of the Value stack.
 * <code>globals</code> is a hash table storing all the global variables.
 * <code>strings</code> is a hashtable containing pointers to all the string objects in the VM, and supports string interning.
 * <code>objects</code> stores all the different kinds of runtime objects for the VM.
 */
typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
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
