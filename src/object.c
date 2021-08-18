//
// Created by Jyotinder Singh on 27/06/21.
//

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// This macro is used as a wrapper to help get the pointer back in the required Object type.
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

/**
 * Helper function that allocates an object of the given size on the heap. The size is not just the size of the Obj itself.
 * Teh caller passes in the number of bytes so that there is room for the extra payload fields needed by the specific
 * object type being created.
 * It then initializes the Obj state.
 * @param size
 * @param type
 * @return
 */
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*) reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d", (void*) object, size, type);
#endif
    return object;
}

/**
 * Utility function to create a new <code>ObjBoundMethod</code>.
 * @param receiver instance to which the method closure is to be bound
 * @param method method closure which needs to be bound.
 * @return Pointer to an ObjMethod containing the method closure bound to the provided receiver.
 */
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

/**
 * Utility function to create a new ObjClass.
 * @param name name of the class to be created.
 * @return
 */
ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    return klass;
}

/**
 * Utility function to create a new ObjClosure instance.
 * @param function
 * @return
 */
ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    // We initialize each piece of allocated memory to make sure the GC won't see any uninitialized piece of memory.
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

/**
 * Utility function to create a new Tok Function.
 * Unlike strings, we create this object in a blank state. We populate the properties later when function gets created.
 * @return
 */
ObjFunction* newFunction() {
    // Allocate memory on the heap for a new Tok function, and return a pointer to it.
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

/**
 * Utility function to create a new instance of a Tok class.
 * @param klass pointer to the CTok class whose instance is to be created
 * @return
 */
ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

/**
 * Utility function to create a new Native Function.
 * Takes a C function pointer to wrap in an ObjNative. It sets up the object header and stores the function.
 * @param function pointer to the native C function.
 * @return
 */
ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

/**
 * Performs the heavy lifting for defining a new string. It acts like a constructor in an OOP language.
 * @param chars
 * @param length
 * @return
 */
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    // push the ObjString on the stack to keep it safe from the GC's while we add it to the intern table.
    // This ensures the string is safe while the table is being resized.
    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    // Now that the ObjString is in the table and reachable by the GC - we can pop it off the VM's stack.
    pop();

    return string;
}

/**
 * Utility function to hash a given string using FNV-1a algorithm.
 */
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t) key[i];
        hash *= 16777619;
    }
    return hash;
}

/**
 * Utility function to allocate a ObjString in the heap, given a char array and it's length.
 * This function takes ownership of the string and passes it into the heap. Because we've already dynamically allocated
 * a character array on the heap, and making a copy of it would be redundant.
 * @param chars
 * @param length
 * @return
 */
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // String interning.
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) {
        // If we found this string in the table, it means we need to free the newly allocated memory for it (which was passed to this function) - since,
        // we would be using the interned reference to it anyway.
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return allocateString(chars, length, hash);
}

/**
 * Utility function to copy a string in the source code to the heap, and return a reference to it.
 * This function assumes that it cannot take ownership of the characters you pass in. Instead, it conservatively creates
 * a copy of the characters on the heap that the ObjString can own.
 * This is the right approach for string literals where the passed-in characters are in the middle of the source string.
 * @param chars character array pointing to the string.
 * @param length length of the string.
 * @return ObjString* pointing to a newly allocated string object on the heap.
 */
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // String interning.
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    // First we allocate a new array on the heap, just big enough for the string's characters and the trailing terminator
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

/**
 * Utility function to print out a function.
 * @param function function object to be printed.
 */
static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

/**
 * Utility function to print the value of an Object.
 * @param value
 */
void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_BOUND_METHOD:
            printFunction(AS_BOUND_METHOD(value)->method->function);
            break;
        case OBJ_CLASS:
            // A class simply says its own name.
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}