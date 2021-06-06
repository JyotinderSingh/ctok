//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_COMMON_H
#define CTOK_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * When this flag is defined we use the debug module to print out the chunk's bytecode.
 */
#define DEBUG_PRINT_CODE
/**
 * When this flag is defined the VM disassembles and prints each instruction right before executing it.
 */
#define DEBUG_TRACE_EXECUTION

#endif //CTOK_COMMON_H
