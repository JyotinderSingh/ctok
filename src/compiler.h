//
// Created by Jyotinder Singh on 04/06/21.
//

#ifndef CTOK_COMPILER_H
#define CTOK_COMPILER_H

#include "chunk.h"
#include "object.h"
#include "vm.h"

bool compile(const char* compile, Chunk* chunk);

#endif //CTOK_COMPILER_H
