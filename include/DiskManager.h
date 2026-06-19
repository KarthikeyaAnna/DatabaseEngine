#pragma once

#include "Config.h"
#include "Page.h"
#include <fstream>

/**
 * ============================================================================
 * DISK MANAGER
 * ============================================================================
 * The Disk Manager acts as the bridge between the database engine and the 
 * physical file on your hard drive. It handles creating the file, allocating 
 * new pages, and reading/writing pages to disk.
 */

struct DiskManager {
    std::fstream* file;   // The open file stream
    char* filename;       // Name of the database file (e.g., "test.db")
    PageID next_page_id;  // The ID that will be given to the next allocated page
};

// Opens the specified file (or creates it if it doesn't exist) and initializes the manager.
DiskManager* disk_manager_create(const char* filename);

// Safely closes the file and frees any memory used by the Disk Manager.
void disk_manager_destroy(DiskManager* dm);

// Generates a new, unique Page ID and increments the internal counter.
PageID disk_manager_allocate_page(DiskManager* dm);

// Writes a 4KB page from memory directly to its correct offset in the file.
void disk_manager_write_page(DiskManager* dm, PageID page_id, const Page* page);

// Reads a 4KB page from the file into memory. Returns false if the page doesn't exist.
bool disk_manager_read_page(DiskManager* dm, PageID page_id, Page* page);

// Returns the total number of pages currently managed by the disk manager.
PageID disk_manager_get_total_pages(const DiskManager* dm);
