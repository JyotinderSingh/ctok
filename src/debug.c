//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }

}

static int constantInstruction(const char *name, Chunk *chunk, int offset) {
    // we pull out the constant index from the byte succeeding the OP_CONSTANT instruction.
    uint8_t constant = chunk->code[offset + 1];
    // we print the name of the opcode being parsed, and the index of the constant value (not particularly useful to us humans)
    printf("%-16s %4d '", name, constant);
    // we also look up the actual constant value, since we do know them at compile time.
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    // OP_CONSTANT is a 2 byte instruction, 1 byte for the opcode and 1 byte for the constant value.
    return offset + 2;
}

static int simpleInstruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassembleInstruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}