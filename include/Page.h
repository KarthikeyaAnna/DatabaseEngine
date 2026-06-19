#pragma once

#include "Config.h"
#include <cstring>

/**
 * ============================================================================
 * PAGE STRUCTURES
 * ============================================================================
 * Our database divides the file into fixed-size "Pages" of 4KB each.
 * Every page serves a specific purpose (storing data, or acting as an index node).
 */

// Identifies what kind of data a page is currently holding.
enum class PageType : uint8_t {
    INVALID = 0,         // Uninitialized or corrupted page
    DATA_PAGE = 1,       // Holds actual key-value records
    BTREE_INTERNAL = 2,  // Holds routing keys and child page pointers for the B+ Tree
    BTREE_LEAF = 3       // Holds keys and pointers to the actual DATA_PAGEs
};

// Every page starts with this 32-byte header to keep track of its metadata.
struct PageHeader {
    PageType type;                  // What kind of page this is
    uint8_t reserved[3];            // Padding for alignment
    PageID page_id;                 // The unique ID of this page
    uint32_t record_count;          // How many items (records or keys) are currently in this page
    uint32_t free_space_offset;     // Where the next piece of data should be written
    PageID next_page_id;            // For leaf nodes: points to the right-sibling leaf node
    uint8_t padding[12];            // Extra padding to ensure the header is exactly 32 bytes
} __attribute__((packed));

// Ensure the compiler doesn't add unexpected padding.
static_assert(sizeof(PageHeader) == 32, "PageHeader must be exactly 32 bytes");

// A single key-value record stored in a DATA_PAGE.
struct Record {
    char key[MAX_KEY_SIZE];
    char value[MAX_VALUE_SIZE];
} __attribute__((packed));

// A routing entry stored in the B+ Tree nodes. 
// It maps a specific key to a child page (or data page).
struct BTreeEntry {
    char key[MAX_KEY_SIZE];
    PageID page_id;
} __attribute__((packed));

// The raw representation of a 4KB page in memory.
// It contains a header at the beginning, followed by the actual data payload.
struct Page {
    char data[PAGE_SIZE]; // The raw 4KB buffer
    
    // Helper to get a mutable pointer to the header
    PageHeader* header() {
        return reinterpret_cast<PageHeader*>(data);
    }
    
    // Helper to get a read-only pointer to the header
    const PageHeader* header() const {
        return reinterpret_cast<const PageHeader*>(data);
    }
    
    // Helper to get a mutable pointer to the data payload (skipping the header)
    char* body() {
        return data + sizeof(PageHeader);
    }
    
    // Helper to get a read-only pointer to the data payload
    const char* body() const {
        return data + sizeof(PageHeader);
    }
    
    // Wipes the page clean and initializes its header
    void init(PageType type, PageID id) {
        ::memset(data, 0, PAGE_SIZE); // Zero out the memory
        PageHeader* h = header();
        h->type = type;
        h->page_id = id;
        h->record_count = 0;
        h->free_space_offset = sizeof(PageHeader);
        h->next_page_id = INVALID_PAGE_ID;
    }
};
