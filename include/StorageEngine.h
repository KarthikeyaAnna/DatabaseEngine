#pragma once

#include "Config.h"
#include "DiskManager.h"
#include "BufferPoolManager.h"
#include "BPlusTree.h"
#include "Page.h"
#include <shared_mutex>

struct StorageEngine {
    DiskManager* disk_manager;
    BufferPoolManager* bpm;
    BPlusTree* index;
    std::shared_mutex rw_lock;
};

// Creates the full database engine
StorageEngine* storage_engine_create(const char* db_file);

// Gracefully shuts down the engine
void storage_engine_destroy(StorageEngine* engine);

// Inserts a new key-value pair into the database.
bool storage_engine_insert(StorageEngine* engine, const char* key, const char* value);

// Looks up a key and copies its associated value
bool storage_engine_search(StorageEngine* engine, const char* key, char* value_out);

// Prints debugging information about the current state of the database.
void storage_engine_print_stats(StorageEngine* engine);
