cmake_minimum_required(VERSION 3.19)
project(ctok C)

set(CMAKE_C_STANDARD 99)

add_executable(ctok src/main.c src/common.h src/chunk.h src/chunk.c src/debug.h src/debug.c src/memory.h src/memory.c src/value.h src/value.c src/vm.c src/vm.h src/compiler.h src/compiler.c src/scanner.h src/scanner.c)