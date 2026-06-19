#pragma once

#include "Config.h"
#include "BufferPoolManager.h"
#include "Page.h"

struct BPlusTree {
    BufferPoolManager* bpm;
    PageID root_page_id;
};

// Initializes the B+ tree structure in memory.
BPlusTree* btree_create(BufferPoolManager* bpm);

// Frees the B+ tree structure from memory.
void btree_destroy(BPlusTree* tree);

// Creates the very first root page of an empty tree and saves it to disk.
void btree_init(BPlusTree* tree);

// Inserts a new key into the tree, pointing to the page where the actual data lives.
bool btree_insert(BPlusTree* tree, const char* key, PageID data_page_id);

// Looks up a key in the tree and returns the ID of the data page where its record is stored.
bool btree_search(BPlusTree* tree, const char* key, PageID* data_page_id);

// Returns the page ID of the root node (useful for statistics).
PageID btree_get_root_page_id(const BPlusTree* tree);
