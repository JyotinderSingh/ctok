//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC

#include <stdio.h>
#include "debug.h"

#endif

#define GC_HEAP_GROW_FACTOR 2

/**
 * reallocate is a single function we use for all the dynamic memory management in ctok -
 * allocating memory, freeing memory, and changing the size of the existing allocation.
 * @param pointer
 * @param oldSize
 * @param newSize
 * @return
 */
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    // Every time we free/allocate some memory, we adjust the counter by that delta.
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
        // when we cross the threshold, we run the GC.
        if (vm.bytesAllocated > vm.nextGC) {
            collectGarbage();
        }
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

/**
 * Utility function to mark an object as referenced. Required for GC.
 * @param object
 */
void markObject(Obj* object) {
    if (object == NULL) return;
    // prevent the GC from getting stuck in a loop, check if the current object has already been visited.
    if (object->isMarked) return;
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*) object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // NOTE: we use raw realloc rather than our own memory management wrapper functions. The memory for the gray stack
        // is not managed by the garbage collector. We don't want to grow the stack during a GC to cause the GC to recursively
        // start a new GC. That could tear a hole in the space-time continuum.
        vm.grayStack = (Obj**) realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

        // In case we fail to grow the grayStack capacity (which is rarer than a new ASOIAF book coming out), we gracefully exit.
        // This must have meant we ran out of memory.
        if (vm.grayStack == NULL) exit(1);
    }

    // Add the object to reference to the grayStack.
    vm.grayStack[vm.grayCount++] = object;
}

/**
 * Function to mark a value as referenced, for the GC.
 * @param value
 */
void markValue(Value value) {
    // We only care about marking objects. Because Tok values such as numbers, booleans, and nil -
    // are stored directly inline in Value and require no heap allocation. So, the GC doesn't need to worry about them.
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

/**
 * Mark all the values present in a ValueArray for the GC process.
 * @param array
 */
static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

/**
 * Utility function to traverse all the references from a given object for the graph coloring process during GC.
 * NOTE: there is no direct encoding of "black" ino the object's state. A black object is any object whose <code>isMarked</code>
 * field is set and that is no longer in the gray stack.
 * @param object
 */
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*) object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    // Each object has different kinds of fields that might reference other objects
    switch (object->type) {
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = (ObjBoundMethod*) object;
            /// mark the receiving instance, so that <code>this</code> can still find the object when the handler is invoked later.
            markValue(bound->receiver);
            // mark the method being bound to the receiver.
            markObject((Obj*) bound->method);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*) object;
            // mark the class's name to keep the string alive.
            markObject((Obj*) klass->name);
            // mark all the methods present on the class.
            markTable(&klass->methods);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*) object;
            // Each closure has a reference to the bare function it wraps
            markObject((Obj*) closure->function);
            // Each closure has an array of pointers to the upvalues is captures.
            for (int i = 0; i < closure->upvalueCount; i++) {
                // Mark each upvalue present in the upvalue array.
                markObject((Obj*) closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*) object;
            // Each function has a reference to an ObjString containing the function's name.
            markObject((Obj*) function->name);
            // Each function has a constant table full of references to other objects.
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*) object;
            // if the instance is alive, we need to keep its class around.
            markObject((Obj*) instance->klass);
            // we need to keep every object referenced by the instance's fields around as well.
            markTable(&instance->fields);
            break;
        }
        case OBJ_UPVALUE:
            // When an upvalue is closed, it contains a reference to the closed-over value.
            // Since the value is no longer on the stack, we need to trace the reference to it from the upvalue.
            markValue(((ObjUpvalue*) object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            // string and native function objects contain no outgoing references so there's nothing to traverse.
            break;

    }
}

/**
 * Utility function to free up particular objects in the memory given their references.
 * @param object
 */
static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*) object, object->type);
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD:
            // Note: While the bound method contains a couple of references, it doesn't own them - so it frees nothing but itself.
            FREE(ObjBoundMethod, object);
            break;
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*) object;
            // The ObjClass struct owns the memory for the methods hash table.
            freeTable(&klass->methods);
            FREE(ObjClass, object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*) object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*) object;
            // Free the chunk present inside the function first.
            freeChunk(&function->chunk);
            // Free the function object itself.
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*) object;
            // The instance owns its field table, so we free the table when freeing the instance.
            // We don't explicity free the entries in the table, because there may be other references to those objects.
            // The GC will take care of those for us. Here we only free the entry array of the table itself.
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_STRING: {
            ObjString* string = (ObjString*) object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
    }
}

/**
 * Marks all the roots for memory references in the heap.
 */
static void markRoots() {
    // Most roots are local variables or temporaries sitting right in the VM's stack, so we start by walking that.
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // VM maintains a separate stack of CallFrames. Each CallFrame contains a pointer to the closure being called.
    // The VM uses those pointers to access constants and upvalues, so they need to be kept around too.
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*) vm.frames[i].closure);
    }

    // The open upvalue list is also a set of values that the VM can directly reach.
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*) upvalue);
    }

    // Mark the roots which originate from global variables.
    markTable(&vm.globals);

    // Collection can begin during any kind of allocation, and not just when the user's program is running.
    // The compiler itself periodically grabs memory from the heap for literals and constant table. If the GC runs
    // while we're in the middle of compiling, then any values the compiler directly accesses need to be treated as roots too.
    markCompilerRoots();
    markObject((Obj*) vm.initString);
}

/**
 * Utility function to trace children of a gray node.
 * Until the stack empties, we keep pulling out gray objects, traversing their references, then marking them black.
 * Traversing an object's references may turn up new white objects that get marked gray and added to the stack.
 */
static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        // traverse the currently picked gray object's references.
        blackenObject(object);
    }
}

/**
 * Sweeps the memory to collect the garbage; all the objects that are not marked - and hence unreachable.
 * This function walks through a list of all the objects checking their mark bits. If an object is marked (black),
 * we leave it alone and continue past it. If it is unmarked (white), we unlink it from the list and free it using the
 * <code>freeObject()</code> function.
 */
static void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            // after sweep completes, only live black objects remain with their mark bits set. We need to unset these bits
            // once we've been through them so that when the next collection cycle starts, every object is white.
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

/**
 * Function to handle the Mark-Sweep Garbage Collection.
 * We use a tricolor abstraction to keep track of where we are in the GC process.
 * Each object has a conceptual color that tracks what state the object is in, and what work is left to do:
 * - White: At the beginning of a garbage collection, every object is white. This color means we have not reached or processed the object at all.
 * - Gray: During marking, when we first reach an object, we darken it gray. This color means we know the object itself
 *   is reachable and should not be collected. But we have not yet traced through it to see what other objects it references.
 *   This is the worklist—the set of objects we know about but haven’t processed yet.
 * - Black: When we take a gray object and mark all of the objects it references, we then turn the gray object black.
 *   This color means the mark phase is done processing that object.
 */
void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif

    // Mark the roots
    markRoots();

    // Process all the gray marked objects.
    traceReferences();

    /**
     * Ctok interns all strings. That means the VM has a hash table containing a pointer to every single string in the heap.
     * The VM uses this to de-duplicate strings.
     * During the mark phase, we deliberately did not treat the VM’s string table as a source of roots.
     * If we had, no string would ever be collected. The string table would grow and grow and never yield a single byte
     * of memory back to the operating system. That would be bad.At the same time,
     * if we do let the GC free strings, then the VM’s string table will be left with dangling pointers to freed memory. That would be even worse.
     * To solve this we use a weak reference to reference the strings in the table.
     * <code>tableRemoveWhite()</code> clears out any dangling pointers for strings that are freed.
     * To remove references to unreachable strings we need to know which strings are unreachable - we dont know that until
     * after the mark phase has completed. But we can't wait until after the sweep phase is done, because by then the
     * objects and their mark bits are no longer around to check. So we do it exactly between marking and sweeping phases.
     */
    tableRemoveWhite(&vm.strings);

    // At this point we have processed all objects we could get our hands on. The grayStack is empty, and every object
    // in the heap is either black or white. The black objects are reachable, and we want to hang on to them. Anything
    // that's still white never got touched by the trace and is thus garbage. We just need to reclaim it.
    sweep();

    // After the collection completes, we adjust the threshold of the next GC based on the number of live bytes that remain.
    // The threshold is a multiple of the heap size. This way, as the amount of memory the program uses grows,
    // the threshold moves farther out to limit the total time spent re-traversing the larger live set
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif

}

/**
 * Function to help cleanup memory references after the VM has completed its operations.
 */
void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    // free the GC resources when the VM shuts down.
    free(vm.grayStack);
}