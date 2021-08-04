//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

/**
 * Function to initialize a chunk before use.
 * @param chunk
 */
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

/**
 * Function to clear out memory from a chunk, and reinitialize it to a stable state.
 * @param chunk
 */
void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

/**
 * Function that is used to append bytecode to chunk->code array and add code to it.
 * Also captures the line numbers for debugging.
 * @param chunk
 * @param byte
 * @param line
 */
void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    // pushing the value on the stack to make sure it doesn't get GC'd lol.
    push(value);
    writeValueArray(&chunk->constants, value);
    // Now that it is in the ValueArray and safe from GC's wrath, we can pop the value off of the VM's stack.
    pop();
    // we return the index where the constant was appended so that it can be located later.
    return chunk->constants.count - 1;
}