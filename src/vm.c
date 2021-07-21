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
    // The CallFrame stack is empty at the start.
    vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // After printing the error message, we start walking the call stack from th etop (the most recently called function)
    // to bottom (the top-level code). For each frame, we find the line number that corresponds to the current ip
    // inside that frame's function. Then we print that line number along with the name of the function.
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
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
 * Initializes a new CallFrame for a Tok Function call,
 * @param function pointer to a ObjFunction for the Object/function being called.
 * @param argCount number of arguments passed to the function.
 * @return
 */
static bool call(ObjFunction* function, int argCount) {
    if (argCount != function->arity) {
        runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    // -1 is for the stack slot zero set aside for method calls.
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

/**
 * Method to handle Tok Function calls.
 * Handles error cases.
 * @param callee
 * @param argCount
 * @return
 */
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_FUNCTION:
                return call(AS_FUNCTION(callee), argCount);
            default:
                break; // Non-callable object type.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
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
    // Reference to the current topmost CallFrame.
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)

/**
 * READ_CONSTANT() treats the next number (in the next byte, to which IP is pointing)
 * as the index for the corresponding Value in the chunk's constant table.
 */
#define READ_CONSTANT() \
    (frame->function->chunk.constants.values[READ_BYTE()])
/**
 * Yanks the next two bytes from the chunk and builds a 16 bit unsigned integer out of them.
 */
#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

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
        disassembleInstruction(&frame->function->chunk, (int) (frame->ip - frame->function->chunk.code));
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
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                // Takes the assigned value from top of the stack and stores it in the stack slot corresponding to the local variable.
                // NOTE: the value is not popped from the stack, since assignment is an expression, and every expression
                // produces a value. The value of an assignment expression is the assigned value itself, so the VM just leaves the value on the stack.
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
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
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                // Read the operand for the instruction (the jump offset)
                uint16_t offset = READ_SHORT();
                // if the current value on the stack (the result of the condition expression) is false, move the ip by the jump offset.
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                // Unconditional jump back by 'offset' number of instructions.
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                // we need to know the function being called and the number of arguments passed to it.
                // We get the latter from the instruction's operand.
                int argCount = READ_BYTE();
                // The argCount also tells us where to find the function on the stack by counting past the argument
                // slots from the top of the stack. We hand this data off to a separate callValue handler.
                // If that returns false, it means teh call caused some sort of runtime error. When that happens, we abort the interpreter.
                // If the callValue() was successful, there will be a new CallFrame stack for the called function.
                // The run() function has its own cached pointer to the current frame, we need to update that.
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                /**
                 * Since the bytecode dispatch loop reads from the <code>frame</code> variable, when the VM goes to
                 * execute the next instruction, it will read the <code>ip</code> from the newly called function's
                 * CallFrame and jump to its code.
                 */
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_RETURN: {
                // When a function returns a value, that value will be on top of the stack.
                // We pop that value out into a result variable.
                Value result = pop();
                // discard the CallFrame.
                vm.frameCount--;
                // if we just discarded the very last CallFrame, it means we've finished executing the top-level code.
                // The entire program is done, so we pop the main script function from the stack and then exit the interpreter.
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                // discard all the slots the callee was using for its parameters and local variables.
                // This includes the same slots the caller used to pass the arguments (if any).
                vm.stackTop = frame->slots;

                // The top of the stack is now right at the beginning of the returning function's stack window.
                // We push the return value back onto the stack at this new, lower location.
                push(result);
                // Update the run() function's cached pointer to the current frame.
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
        }
    }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

/**
 * First we pass the source code to the compiler. It returns a new <code>ObjFunction</code> containing the compiled
 * top-level code. If we get <code>NULL</code> back, it means there was some compile-time error which the compiler has already reported.
 * In that case, we can't run anything and we bail out.
 * Otherwise, we store the function on the stack and prepare an initial CallFrame to execute its code.
 * This get stored in the already set aside stack slot zero. In the new CallFrame, we point to the function,
 * initialize its <code>ip</code> to point to the beginning of the function's bytecode, and set up its stack window
 * to start at the very bottom of the VM's stack.
 * After finishing, we just run the bytecode we just produced.
 * @param source
 * @return
 */
InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    call(function, 0);

    return run();

}
