//
// Created by Jyotinder Singh on 02/07/21.
//

#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

// Defines the load factor for the hash table.
#define TABLE_MAX_LOAD 0.75

/**
 * Utility function to initialize an empty hash table.
 * @param table
 */
void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

/**
 * Utility function to free up the hash table from the memory.
 * We mainly only need to free up the entries array, the rest of the data structure is just re-initialized to default state.
 * @param table
 */
void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

/**
 * Utility function to fetch the corresponding bucket to a given key in the hashmap.
 * Uses linear probing to handle collisions.
 *
 * Design Note (Handling Tombstones):
 * If we reach a truly empty entry, then the key isn’t present.
 * In that case, if we have passed a tombstone, we return its bucket instead of the later empty one.
 * If we’re calling findEntry() in order to insert a node, that lets us treat the tombstone bucket as empty and
 * reuse it for the new entry. Reusing tombstone slots automatically like this helps reduce the number of tombstones wasting space in the bucket array.
 * @param entries
 * @param capacity
 * @param key
 * @return
 */
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash % capacity;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // We found the key.
            return entry;
        }

        index = (index + 1) % capacity;
    }

}

/**
 * Given a key, the function looks up the corresponding value in the table.
 * If the entry exists, the 'value' output parameter points to the resulting value.
 * @param table
 * @param key
 * @param value output parameter, points to the value at the given key - if key exists.
 * @return If an entry with the given key exists it returns true, otherwise it returns false.
 */
bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

/**
 * Utility function to allocate a fresh entries array for a new hash table, and also to adjust entries in an existing
 * hash table when growing its size.
 * @param table
 * @param capacity
 */
static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; ++i) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;

    // Rehashing the non-null entries in the old hash table to adjust them into the new table.
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        // we pass the new array into findEntry to store the values into this newly allocated array.
        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);

    table->entries = entries;
    table->capacity = capacity;
}

/**
 * Utility function to set add/replace a key/value pair in a hash table.
 * @param table
 * @param key
 * @param value
 * @return Returns true if a new entry was added, returns false if the key already existed and only the value was replaced.
 */
bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;

    // We increment the count during insertion only if the new entry goes into an entirely empty bucket.
    // If we are replacing a tombstone with a new entry, the bucket has already been accounted for and the count doesn't change.
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

/**
 * Deletes an entry from the table.
 * Places a tombstone in its place to prevent lookup errors in case of colliding keys.
 * @param table
 * @param key
 * @return true if deletion was successful, false if element did not exist.
 */
bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // Place tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

/**
 * Utility function to copy all the entries of one hash table to another.
 * @param from
 * @param to
 */
void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

/**
 * Utility function we use for string interning.
 * @param table
 * @param chars
 * @param length
 * @param hash
 * @return
 */
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}