//
// Created by Jyotinder Singh on 01/06/21.
//

#include <stdio.h>

#include "debug.h"
#include "value.h"

/**
 * Function to disassemble instructions in a chunk.
 * @param chunk chunk being disassembled
 * @param name name of the instruction
 */
void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }

}

/**
 * Function to handle opcodes dealing with constants.
 * @param name name of the instruction
 * @param chunk chunk being disassembled
 * @param offset offset of the instruction in the chunk's code array.
 * @return
 */
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
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

/**
 * Function to output debug information for simple instructions - just the name of the instruction.
 * @param name name of the instruction
 * @param offset offset of the instruction in the chunk's code array.
 * @return
 */
static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

/**
 * Function to output debug information for simple byte instructions.
 * @param name name of the instruction
 * @param chunk chunk being disassembled
 * @param offset offset of the instruction in the chunk's code array.
 * @return
 */
static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

/**
 * Function to output the debug info for a jump instruction.
 * @param name name of the instruction
 * @param sign direction of the jump
 * @param chunk chunk being disassembled
 * @param offset offset of the instruction in the chunk's code array.
 * @return
 */
static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    // Read in the 2-byte (16 bit) jump offset
    uint16_t jump = (uint16_t) (chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

/**
 * Function to disassemble an instruction present inside a chunk at a given offset.
 * @param chunk chunk being disassembled
 * @param offset offset of the instruction in the chunk's code array.
 * @return
 */
int disassembleInstruction(Chunk* chunk, int offset) {
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
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}