//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_DEBUG_H
#define CTOK_DEBUG_H

#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char* name);

int disassembleInstruction(Chunk* chunk, int offset);

#endif //CTOK_DEBUG_H
