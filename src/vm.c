//
// Created by Jyotinder Singh on 02/06/21.
//

#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "vm.h"

// Static vm instance for now. Might implement a more dynamic solution later.
VM vm;

/**
 * Function to reset the VM's stack
 * Resets the stack's top pointer to the first element
 * not that we don't need to really clear out the values from the array itself
 */
static void resetStack() {
    vm.stackTop = vm.stack;
}

/**
 * Function to initialize the VM before use.
 */
void initVM() {
    resetStack();
}

void freeVM() {}

/**
 * Push a value into the VM's stack
 * @param value
 */
void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

/**
 * Pop and return the latest element pushed into the stack.
 * @return
 */
Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
/**
 * Read constant treats the next number (in the next byte to which IP is pointing)
 * as the index for the corresponding Value in the chunk's constant table.
 */
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
    for (;;) {

/**
 * If the DEBUG_TRACE_EXECUTION flag is defined the debugger disassembles the instructions dynamically.
 */
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        /**
         * The offset is supposed to be an integer byte offset,
         * hence we do a little pointer math to convert ip back to a relative offset from the beginning of the bytecode.
         */
        disassembleInstruction(vm.chunk, (int) (vm.ip - vm.chunk->code));
#endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NEGATE:
                push(-pop());
                break;
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }
#undef READ_BYTE
#undef READ_CONSTANT
}

InterpretResult interpret(Chunk *chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;
    return run();
}
