//
// Created by Jyotinder Singh on 01/06/21.
//

#ifndef CTOK_COMMON_H
#define CTOK_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * When defined, enables memory optimized NaN Boxing representation for values. If your chip does not support this, remove this flag.
 */
#define NAN_BOXING

/**
 * When this flag is defined we use the debug module to print out the chunk's bytecode.
 */
//#define DEBUG_PRINT_CODE
/**
 * When this flag is defined the VM disassembles and prints each instruction right before executing it.
 */
//#define DEBUG_TRACE_EXECUTION

/**
 * Optional "stress test" mode for the garbage collector.
 * When this flag is defined, the GC runs as often as it possibly can.
 */
//#define DEBUG_STRESS_GC

/**
 * When enabled, ctok prints information to the console when it does something with dynamic memory.
 */
//#define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

#endif //CTOK_COMMON_H
