//
// Created by Jyotinder Singh on 02/06/21.
//

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

// Static vm instance for now. Might implement a more dynamic solution later.
VM vm;

/**
 * Native clock function that returns the elapsed time since the program started running, in seconds.
 * Handy for benchmarking.
 * @param argCount
 * @param args
 * @return elapsed time since the program started running, in seconds
 */
static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

/**
 * Function to reset the VM's stack
 * Resets the stack's top pointer to the first element
 * not that we don't need to really clear out the values from the array itself
 */
static void resetStack() {
    vm.stackTop = vm.stack;
    // The CallFrame stack is empty at the start.
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

/**
 * Handles runtime errors.
 * @param format
 * @param ...
 */
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
        ObjFunction* function = frame->closure->function;
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
 * Helper to define a new native function.
 * Takes a poniter to a C function and the name it will be knows as in Tok. We wrap the function in an ObjNative
 * and then store that in a global variable with the given name.
 * NOTE: we push and pop the name and the function on the stack because both <code>copyString()</code> and
 * <code>newNative</code> dynamically allocate memory. That means they can potentially trigger a garbage collection in our
 * Garbage Collector. If that happens, we need to ensure the collector knows we're not done with the name and ObjFunction
 * so that it doesn't free them out from under us. Storing them as a stack value accomplishes that.
 * @param name Name by which the native function will be known from, in Tok.
 * @param function pointer to the native C function.
 */
static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int) strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

/**
 * Function to initialize the VM before use.
 */
void initVM() {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    // GC initializations.
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);

    // initialize the initString with the reserved keyword for defining initializer functions.
    // Since during the copyString operation, we could trigger a GC. If the collector ran at just
    // the wrong time, it would read vm.initString before it had been initialized. So, first we zero the field out.
    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    // initialize native functions.
    defineNative("clock", clockNative);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    vm.initString = NULL;
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
static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    // -1 is for the stack slot zero set aside for method calls.
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

/**
 * Handles a call invocation on a Value. Method to handle Tok Function calls.\n
 * Handles error cases.
 * @param callee object on which the call is being invoked.
 * @param argCount number of arguments passed to the call.
 * @return
 */
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                // Place the receiver at stack slot zero for the method to access in case of "this" invocation.
                // When a method is called, the top of the stack contains all the arguments,
                // and then just under this is the closure of the called method. That's where slot zero in the new CallFrame will be.
                // The -argCount skips past the arguments and the - 1 adjusts for the fact that stackTop points just past the last used stack slot.
                vm.stackTop[-argCount - 1] = bound->receiver;
                // pull out the raw closure from the ObjBoundMethod invoke the call.
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                // if the value being called is a class, we treat it as constructor call.
                ObjClass* klass = AS_CLASS(callee);
                // we create a new instance of the called class and store the result on the stack, at stack slot zero.
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

                // automatically calling init() on new instances.
                // After ht runtime allocates the new instance, we look for an init() method on the class.
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    // if we find the init() method, we initiate a call to it. This pushes a new CallFrame for the initializer's closure.
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    // Tok doesn't require a class to define an initializer. If omitted, the runtime simply returns the
                    // new uninitialized instance. However, if there is no init() method, then it doesn't make any sense to
                    // pass arguments to the class when creating an instance. We raise an error in that case.
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                // If the object being called is a native function, we invoke the C function right here.
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                // Stuff the result back into the stack.
                push(result);
                return true;
            }
            default:
                break; // Non-callable object type.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

/**
 * Invokes a given method on a given class.
 * @param klass The Tok class on which the method needs to be invoked.
 * @param name name of the method being invoked.
 * @param argCount number of arguments provided to the method.
 * @return true if method call succeeds, false otherwise.
 */
static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    // find the method in the class's method table.
    if (!tableGet(&klass->methods, name, &method)) {
        // throw an error if the method is not present.
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    // push a call to the method's closure onto the CallFrame stack.
    return call(AS_CLOSURE(method), argCount);
}

/**
 * Handles an OP_INVOKE call for an optimized method call flow.
 * @param name name of the method being called.
 * @param argCount number of arguments passed to the method.
 * @return true if invocation succeeded, false otherwise.
 */
static bool invoke(ObjString* name, int argCount) {
    // grab the receiver off the stack.
    Value receiver = peek(argCount);
    // cast receiver Value as instance.
    if (!IS_INSTANCE(receiver)) {
        // report error and bail out.
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    // first we look up a field with the given name
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        // if found, we store it on the stack in the place of the receiver, under the argument list. (The way OP_GET_PROPERTY
        // behaves, since the latter instruction executes before a subsequent paranthesized list of arguments has been evaluated).
        vm.stackTop[-argCount - 1] = value;
        // try to call the field's value like the callable that it hopefully is.
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

/**
 * Binds a method call to an instance.\n
 * Takes a class of an instance and a name of a method, and places the corresponding the ObjBoundMethod object on top of the stack.
 * @param class name of the class.
 * @param name name of the method to be looked up.
 * @return true if method was found, otherwise, false.
 */
static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    // look for the given method in the class's method table. If we don't find one, we report a runtime error and bail out.
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    // if we find the method, we wrap it in a new ObjBoundMethod (binding it to the instance on top of the stack).
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    // pop the receiver/instance from the stack.
    pop();
    // push the bound method on top of the stack
    push(OBJ_VAL(bound));
    return true;
}

/**
 * Function to close over a local variable.
 * @param local pointer to the captured local's slot in the surrounding function's stack window.
 * @return reference to the ObjUpvalue that was dynamically allocated.
 */
static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;

    // We want all the references to a closed over local variable to share the same copy of the upvalue. Hence, we do the following:
    // Start at the head of the openUpvalue's list, which is the upvalue closest to the top of the stack.
    // We walk through the list iterating past every upvalue pointing to slot above the one we're looking for.
    // While we do that, we keep track of the preceding upvalue on the list - since, we'll need to update that node's
    // next pointer if we end up inserting a node after it.
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // condition: We found an existing upvalue capturing the variable, so we reuse this upvalue.
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    // We reach here in either of two cases:
    // 1. we exited the list traversal by going past the end of the list.
    // 2. we exited the list traversal by stopping on the first upvalue whose stack slot is below the one we're looking for.
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    // In either case, we need to insert the new upvalue before the object pointed at by the upvalue (which may be NULL if we hit the end of the list)
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

/**
 * Function responsible for closing an upvalue.
 * Takes a pointer to a stack slot. It closes every open upvalue it can find that points to that slot or any slot above
 * it on the stack.
 * Upvalues get closed as follows:
 * First, we copy the variable's value into the <code>closed</code> field in the ObjUpvalue. That's where the closed-over
 * variables live on the heap. The <code>OP_GET_UPVALUE</code> and <code>OP_SET_UPVALUE</code> instructions need to look
 * for the variable there after it's been moved.
 * @param last pointer to a stack slot where the variable to be closed resides.
 */
static void closeUpvalues(Value* last) {
    // We walk the VM's list of open upvalues, from top to bottom. If an upvalue's location points into a range of slots
    // we're closing, we close the upvalue. Otherwise, once we reach an upvalue outside of the range, we know the rest
    // will be too, so we stop iterating.
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

/**
 * Adds a method closure present at the top of the stack to the <code>methods</code> hash table of a given object.
 * @param name
 */
static void defineMethod(ObjString* name) {
    // read the method closure present on the top of the stack.
    Value method = peek(0);
    // read the class object present below it
    ObjClass* klass = AS_CLASS(peek(1));
    // add the method to the hash table of the class object.
    tableSet(&klass->methods, name, method);
    // pop the closure from the stack since we're done with it.
    pop();
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
    // peek the strings and don't pop them just yet - since otherwise they might end up being GC'd during the ALLOCATE call.
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
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
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
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
        disassembleInstruction(&frame->closure->function->chunk,
                               (int) (frame->ip - frame->closure->function->chunk.code));
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
            case OP_GET_UPVALUE: {
                // index into the current function's upvalue array.
                uint8_t slot = READ_BYTE();
                // we look up the corresponding upvalue and dereference its location pointer to read the value in that slot.
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                // pick the value on the top of the stack and store it into the slot pointed to by the chosen upvalue.
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                // When the interpreter reaches this instruction, the expression to the left of the dot has already been
                // executed and the resulting instance is on top of the stack.
                if (!IS_INSTANCE(peek(0))) {
                    // In Tok only instances are allowed to have fields, you can't stuff a field on a string or a number.
                    // So we check for that before trying to access any fields on it.
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjInstance* instance = AS_INSTANCE(peek(0));
                // We read the field name from the constant pool
                ObjString* name = READ_STRING();

                // and look it up in the instance's field table.
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    // If the hash table contains an entry with that name, we pop the instance and push the entry's value as the result
                    pop();
                    push(value);
                    break;
                }

                // fields take priority over and shadow methods, hence method is checked here after field lookup.
                // if the instance does not have a field with the given property name, then the name may refer to a method.
                // we take the instance's class and pass it to bindMethod(). If bindMethod finds a method, it places the
                // method on the stack and returns true, otherwise, returns false.
                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                // make sure we're trying to set a property on an instance and nothing else
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // When this executes, the top of the stack has the instance whose field is being set, and above that,
                // the value to be stored.
                ObjInstance* instance = AS_INSTANCE(peek(1));
                // We first read the instruction's operand and find the field name string.
                tableSet(&instance->fields, READ_STRING(), peek(0));
                // we get the value to be stored off the stack.
                Value value = pop();
                // we pop the instance itself off
                pop();
                // finally, push the value back on the stack.
                push(value);
                break;
            }
            case OP_GET_SUPER: {
                // read the method name from the constant table.
                ObjString* name = READ_STRING();
                // Load up the superclass object, which the compiler has placed on top of the stack.
                ObjClass* superclass = AS_CLASS(pop());

                // bind the method to the superclass.
                if (!bindMethod(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
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
            case OP_INVOKE: {
                // name of the method being called.
                ObjString* method = READ_STRING();
                // number of arguments being passed to the method.
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // if method invocation succeeded, then there is a new CallFrame on the stack, so we refresh our cached
                // copy of the current frame in frame.
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_SUPER_INVOKE: {
                //Optimized flow for super method invocation.
                // The main difference is in how the stack is organized.
                // read the name of the method being called.
                ObjString* method = READ_STRING();
                // read the number of arguments passed to the method.
                int argCount = READ_BYTE();
                // load the superclass being referred to.
                ObjClass* superclass = AS_CLASS(pop());
                // invoke the method call.
                if (!invokeFromClass(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // update the cached local frame if the invocation succeeded, since now a new CallFrame has been pushed to the CallFrame stack.
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                // Read the function object from the constant table.
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                // Wrap it in a closure object and push it onto the stack.
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));

                // We iterate over each upvalue the closure expects. For each one, we read a pair of operand bytes.
                // If the upvalue closes over a local variable in the enclosing function, we let captureUpvalue() do the work.
                // Otherwise, we capture an upvalue from the surrounding function. An OP_CLOSURE instruction is emitted
                // at the end of a function declaration. At the moment we are executing that declaration, the current
                // function is the surrounding one. That means the current function's closure is stored in the CallFrame
                // at the top of the callstack. So, to grab an upvalue from the enclosing function, we can read it right
                // here from the frame local variable, which caches a reference to that CallFrame.
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        // We need to calculate the argument to pass to captureUpvalue. We need to grab a pointer to
                        // the captured local's slot in the surrounding function's stack window. That window begins at
                        // frame->slots, which points to slot zero. Adding 'index' offsets that to the local slot we want to capture.
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                // The variable we want to hoist is at the top of the stack. We pass the address of the variable's stack slot
                // to closeUpvalues, which is responsible for closing the upvalue and moving the local from the stack to the heap.
                closeUpvalues(vm.stackTop - 1);
                // After that, the VM is free to discard the stack slot, which it does by calling pop()
                pop();
                break;
            case OP_RETURN: {
                // When a function returns a value, that value will be on top of the stack.
                // We pop that value out into a result variable.
                Value result = pop();
                // The compiler does not emit any instructions at the end of the outermost block scope that defines a
                // function body. That scope contains the function's parameters and any locals declared immediately inside the function.
                // Those need to get closed too, so we do that here.
                // By passing the first slot in the function's stack window, we close every remaining open upvalue owned
                // by the returning function.
                closeUpvalues(frame->slots);
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
            case OP_CLASS:
                // Load the string for the class's name from the constant table and pass that to newClass().
                // This creates a new class object with the given name. We then push this onto the stack.
                // If the class is bound to a global variable, then the compiler's call to defineVariable() will emit
                // code to store that object from the stack into the global variable table. Otherwise, it's right where
                // it needs to be on the stack for a new local variable.
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_INHERIT: {
                // get the superclass
                Value superclass = peek(1);

                // check if the user is trying to inherit from a valid class.
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // get the subclass
                ObjClass* subclass = AS_CLASS(peek(0));
                // copy all the superclass's methods into the subclass.
                // by the time the subclass's body is about to be parsed, all the methods of the superclass are
                // present in the subclass's own method table. Hence, no extra work needs to be done at runtime.
                tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
                pop();  // pop the subclass
                break;
            }
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
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

    // We wrap the raw function returned by the compiler in a closure, and pass it into the VM.
    // We push it onto the stack to make sure the GC won't clean it up in the middle of execution.
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();

}
