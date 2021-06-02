//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_CHUNK_H
#define CTOK_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
    OP_RETURN,
    OP_CONSTANT
} OpCode;

typedef struct {
    int count;
    int capacity;
    // array of bytes.
    uint8_t *code;
    int* lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);

void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte, int line);

int addConstant(Chunk *chunk, Value value);

#endif //CTOK_CHUNK_H
