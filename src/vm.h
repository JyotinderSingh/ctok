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
 * The <code>closure</code> pointer points to the closure of the function being called.
 * We use that to look up constants, and a few other things. Each time a function is called, we create one of these structs.
 */
typedef struct {
    ObjClosure* closure;
    /**
     * Each frame will store it's own <code>ip</code>. When we return from a function, the VM will jump to the <code>ip</code>
     * of the caller's CallFrame and resume from there.
     */
    uint8_t* ip;
    /**
     * The <code>slots</code> field points into the VM's value stack at the first slot that this function can use.
     */
    Value* slots;
} CallFrame;

/**
 * Struct for managing the VM instance.
 */
typedef struct {
    /// <code>frames</code> is an array for storing the CallFrames for function calls.
    CallFrame frames[FRAMES_MAX];
    /// <code>frameCount</code> stores the current height of the CallFrame stack.
    int frameCount;
    /// <code>stack</code> contains all the runtime values for the VM.
    Value stack[STACK_MAX];
    /// <code>stackTop</code> stores the pointer to the top of the Value stack.
    Value* stackTop;
    /// <code>globals</code> is a hash table storing all the global variables.
    Table globals;
    /// <code>strings</code> is a hashtable containing pointers to all the string objects in the VM, and supports string interning.
    Table strings;
    /// keyword used for init methods on classes, defined here for performance gains incurred by string interning.
    ObjString* initString;
    /// <code>openUpvalues</code> is the list of open upvalues present in the VM at a particular instant of time.
    ObjUpvalue* openUpvalues;
    /// Running total of the number of bytes of managed memory the VM has allocated.
    size_t bytesAllocated;
    //// Threshold number of bytes that triggers the next collection.
    size_t nextGC;
    /// <code>objects</code> stores all the different kinds of runtime objects for the VM.
    Obj* objects;
    /// Number of gray nodes present in the GC grayStack.
    int grayCount;
    /// Number of gray nodes that the GC grayStack can hold.
    int grayCapacity;
    /// Data structure to hold a pointers to Obj* that have been marked gray by the GC.
    Obj** grayStack;
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
