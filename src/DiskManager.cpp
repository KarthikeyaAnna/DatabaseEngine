#include "DiskManager.h"
#include <cstring>
#include <cstdlib>

using namespace std;

// Initialize the disk manager and open the underlying database file.
DiskManager* disk_manager_create(const char* filename) {
    DiskManager* dm = (DiskManager*)malloc(sizeof(DiskManager));
    if (!dm) return nullptr;
    
    // Store the filename for reference
    dm->filename = (char*)malloc(strlen(filename) + 1);
    strcpy(dm->filename, filename);
    
    dm->file = new ::fstream();
    
    // Attempt to open the file in binary read/write mode
    dm->file->open(filename, ::ios::in | ::ios::out | ::ios::binary);
    
    // If the file doesn't exist, we need to create it first
    if (!dm->file->is_open()) {
        dm->file->open(filename, ::ios::out | ::ios::binary); // Create file
        dm->file->close();
        dm->file->open(filename, ::ios::in | ::ios::out | ::ios::binary); // Re-open with R/W
    }
    
    // Calculate how many pages already exist in the file to determine the next available PageID.
    // We do this by checking the total file size and dividing by the page size.
    dm->file->seekg(0, ::ios::end);
    size_t file_size = dm->file->tellg();
    dm->next_page_id = file_size / PAGE_SIZE;
    
    return dm;
}

// Clean up resources used by the disk manager
void disk_manager_destroy(DiskManager* dm) {
    if (!dm) return;
    
    if (dm->file) {
        if (dm->file->is_open()) {
            dm->file->close();
        }
        delete dm->file;
    }
    
    if (dm->filename) {
        free(dm->filename);
    }
    
    free(dm);
}

// Allocate the next available page ID. This doesn't write to disk immediately;
// it just reserves the ID for the caller to use.
PageID disk_manager_allocate_page(DiskManager* dm) {
    return dm->next_page_id++;
}

// Write the 4KB page buffer to the file at the correct location.
void disk_manager_write_page(DiskManager* dm, PageID page_id, const Page* page) {
    size_t offset = page_id * PAGE_SIZE;
    dm->file->seekp(offset);                     // Move file pointer to the correct offset
    dm->file->write(page->data, PAGE_SIZE);      // Write exactly 4KB
    dm->file->flush();                           // Ensure it hits the disk
}

// Read a 4KB page from the file into the provided memory buffer.
bool disk_manager_read_page(DiskManager* dm, PageID page_id, Page* page) {
    size_t offset = page_id * PAGE_SIZE;
    
    // First, check if the file is large enough to contain this page
    dm->file->seekg(0, ::ios::end);
    size_t file_size = dm->file->tellg();
    if (offset + PAGE_SIZE > file_size) {
        return false; // Page doesn't exist on disk yet
    }
    
    // Read the page data
    dm->file->seekg(offset);
    dm->file->read(page->data, PAGE_SIZE);
    
    // Verify we actually read a full page
    return dm->file->gcount() == PAGE_SIZE;
}

// Returns how many pages have been allocated so far.
PageID disk_manager_get_total_pages(const DiskManager* dm) {
    return dm->next_page_id;
}
