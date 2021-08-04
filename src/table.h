//
// Created by Jyotinder Singh on 02/07/21.
//

#ifndef CTOK_TABLE_H
#define CTOK_TABLE_H

#include "common.h"
#include "value.h"

/**
 * Data structure that backs the key/value implementation in the hash table.
 */
typedef struct {
    ObjString* key;
    Value value;
} Entry;

/**
 * This is the Hash Table data structure used by Tok to achieve various functionalities.
 */
typedef struct {
    int count;      // Number of key/value pairs stored in the table.
    int capacity;   // Capacity of the table / allocated size of the underlying array.
    Entry* entries;
} Table;

void initTable(Table* table);

void freeTable(Table* table);

bool tableGet(Table* table, ObjString* key, Value* value);

bool tableSet(Table* table, ObjString* key, Value value);

bool tableDelete(Table* table, ObjString* key);

void tableAddAll(Table* from, Table* to);

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

void tableRemoveWhite(Table* table);

void markTable(Table* table);

#endif //CTOK_TABLE_H
