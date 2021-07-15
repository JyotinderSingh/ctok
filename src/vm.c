//
// Created by Jyotinder Singh on 02/06/21.
//

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
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

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

/**
 * Function to initialize the VM before use.
 */
void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

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

/**
 * Peeks the values in the stack at a given distance.
 * @param distance
 * @return
 */
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

/**
 * Utility function to check falsiness of a Tok value.
 * Tok follows Ruby in that nil and false are falsey and every other value behaves like true.
 * @param value
 * @return
 */
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/**
 * String utility function used to concatenate two strings.
 */
static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

/**
 * Responsible for handling all the bytecode interpretation.
 * @return <code>InterpretResult</code> indicating whether interpretation was successful or not.
 */
static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
/**
 * READ_CONSTANT() treats the next number (in the next byte, to which IP is pointing)
 * as the index for the corresponding Value in the chunk's constant table.
 */
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

/**
 * Yanks the next two bytes from the chunk and builds a 16 bit unsigned integer out of them.
 */
#define READ_SHORT() \
    (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
/**
 * READ_STRING() treats the next number (in the next byte, to which IP is pointing)
 * as the index for the corresponding ObjString in the chunk's constant table.
 */
#define READ_STRING() AS_STRING(READ_CONSTANT())

// Boilerplate for underlying implementation of all the binary operators.
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)


// Looping over all the instructions in the current chunk.
    for (;;) {

// If the DEBUG_TRACE_EXECUTION flag is defined the debugger disassembles the instructions dynamically.
#ifdef DEBUG_TRACE_EXECUTION
        /**
         * We print the stack with every instruction we execute.
         */
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
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
            case OP_NIL:
                push(NIL_VAL);
                break;
            case OP_TRUE:
                push(BOOL_VAL(true));
                break;
            case OP_FALSE:
                push(BOOL_VAL(false));
                break;
            case OP_POP:
                pop();
                break;
            case OP_GET_LOCAL: {
                // Takes a single byte operand for the stack slot where the local lives.
                // It loads the value from that index and then pushes it on top of the stack where later instructions can find it.
                uint8_t slot = READ_BYTE();
                push(vm.stack[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                // Takes the assigned value from top of the stack and stores it in the stack slot corresponding to the local variable.
                // NOTE: the value is not popped from the stack, since assignment is an expression, and every expression
                // produces a value. The value of an assignment expression is the assigned value itself, so the VM just leaves the value on the stack.
                uint8_t slot = READ_BYTE();
                vm.stack[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                // read the name of the variable.
                ObjString* name = READ_STRING();
                Value value;
                // Check if the variable is actually present in the hash table.
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                // push the value of the variable to the stack.
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                // read the name of the variable.
                ObjString* name = READ_STRING();
                // Add the variable to the hashtable, with it's name as the key, and value as value.
                tableSet(&vm.globals, name, peek(0));
                // we pop the value of the variable after we've added it to the table.
                // This ensure that the VM can still find the value if a garbage collection is triggered right in the
                // middle of adding it to the hashtable. This is a distinct possibility since the hash table requires
                // dynamic allocation when it resizes.
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                // Read the name of the variable.
                ObjString* name = READ_STRING();
                // Try setting the value of the variable in the hash table.
                // - If it returns true - it means the key didn't exist in the table before, and was just added. This
                //   This means that the variable did not before, and was just added.
                //   We don't want this, so we delete it from the table, and throw a runtime error.
                // - If it returns false - it means the key already existed on the table, and only its value was updated - which is what we want.
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                // NOTE: We don't pop the value off the stack in the end, since assignment is an expression so it needs to
                // leave the value in there in case the assignment is nested inside some larger expression.
                break;
            }
            case OP_EQUAL: {
                // get the two operands
                Value b = pop();
                Value a = pop();
                // push the boolean result to the stack
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:
                BINARY_OP(BOOL_VAL, >);
                break;
            case OP_LESS:
                BINARY_OP(BOOL_VAL, <);
                break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    // if operands are strings, perform concatenation.
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    // if operands are numbers, perform arithmetic addition.
                    // get the two operands from the stack.
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    // push the result onto the stack.
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError(
                            "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT:
                BINARY_OP(NUMBER_VAL, -);
                break;
            case OP_MULTIPLY:
                BINARY_OP(NUMBER_VAL, *);
                break;
            case OP_DIVIDE:
                BINARY_OP(NUMBER_VAL, /);
                break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:
                // make sure the operand is a number.
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                // push the result to the stack.
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_PRINT: {
                // pop the value from the stack when printing it.
                // This is because print is a statement, and statements must have a net 0 impact on the stack.
                // The value must have been pushed on the stack as a part of evaluating the expression following the TOKEN_PRINT.
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                // Unlike OP_JUMP_IF_FALSE, this is an unconditional jump forward by 'offset' instruction.
                vm.ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                // Read the operand for the instruction (the jump offset)
                uint16_t offset = READ_SHORT();
                // if the current value on the stack (the result of the condition expression) is false, move the ip by the jump offset.
                if (isFalsey(peek(0))) vm.ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                // Unconditional jump back by 'offset' number of instructions.
                vm.ip -= offset;
                break;
            }
            case OP_RETURN: {
                // Exit Interpreter.
                return INTERPRET_OK;
            }
        }
    }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    Chunk chunk;
    initChunk(&chunk);

    /**
     * We create a new empty chunk and pass it over to the compiler, the compiler will take the user's program
     * and fill up the chunk with bytecode if the program doesn't have any errors.
     * If the program has errors, compile() returns false and we discard the unusable chunk.
     */
    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    /**
     * If the user's code compiled correctly, we send the completed chunk over to the VM to be executed.
     */
    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    /**
     * Free up the memory once the chunk has been run and is no longer needed.
     */
    freeChunk(&chunk);
    return result;
}
