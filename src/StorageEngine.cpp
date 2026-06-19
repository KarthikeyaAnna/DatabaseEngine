#include "StorageEngine.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <cstdio>

using namespace std;

// Initialize the entire database system.
StorageEngine* storage_engine_create(const char* db_file) {
    StorageEngine* engine = new StorageEngine();
    if (!engine) return nullptr;
    
    // Set up the subsystem that writes bytes to disk
    engine->disk_manager = disk_manager_create(db_file);
    if (!engine->disk_manager) {
        delete engine;
        return nullptr;
    }
    
    // Set up Buffer Pool Manager (cache 50 pages)
    engine->bpm = new BufferPoolManager(50, engine->disk_manager);
    
    // Set up the B+ Tree indexing subsystem using the Buffer Pool
    engine->index = btree_create(engine->bpm);
    if (!engine->index) {
        delete engine->bpm;
        disk_manager_destroy(engine->disk_manager);
        delete engine;
        return nullptr;
    }
    
    return engine;
}

// Shut down the database cleanly.
void storage_engine_destroy(StorageEngine* engine) {
    if (!engine) return;
    
    if (engine->index) {
        btree_destroy(engine->index);
    }
    
    if (engine->bpm) {
        delete engine->bpm;
    }
    
    if (engine->disk_manager) {
        disk_manager_destroy(engine->disk_manager);
    }
    
    delete engine;
}

// Adds a new record to our database.
bool storage_engine_insert(StorageEngine* engine, const char* key, const char* value) {
    // Acquire EXCLUSIVE WRITE lock. Blocks all other readers and writers until this finishes.
    std::unique_lock<std::shared_mutex> lock(engine->rw_lock);

    // 1. Fetch a new data page from the Buffer Pool
    PageID data_page_id;
    Page* data_page = engine->bpm->new_page(&data_page_id);
    if (!data_page) return false; // Buffer pool is full of pinned pages!
    
    data_page->init(PageType::DATA_PAGE, data_page_id);
    
    // 2. Write the user's key and value into this data page.
    Record* rec = reinterpret_cast<Record*>(data_page->body());
    
    // Using snprintf to ensure we don't overflow the buffers and guarantee null-termination
    snprintf(rec->key, MAX_KEY_SIZE, "%s", key);
    snprintf(rec->value, MAX_VALUE_SIZE, "%s", value);
    
    // 3. Update the metadata for this data page to reflect that it holds 1 record
    PageHeader* h = data_page->header();
    h->record_count = 1;
    h->free_space_offset = sizeof(PageHeader) + sizeof(Record);
    
    // 4. Unpin the page and mark it DIRTY so the Buffer Pool knows to write it to disk eventually
    engine->bpm->unpin_page(data_page_id, true);
    
    // 5. Finally, tell the B+ Tree index where we put the data so we can find it later!
    return btree_insert(engine->index, key, data_page_id);
}

// Retrieves a value from the database given a key.
bool storage_engine_search(StorageEngine* engine, const char* key, char* value_out) {
    // Acquire SHARED READ lock. Multiple readers can execute this concurrently, but blocks writers.
    std::shared_lock<std::shared_mutex> lock(engine->rw_lock);

    PageID data_page_id;
    
    // 1. Ask the B+ Tree index: "Do you know where this key is?"
    if (!btree_search(engine->index, key, &data_page_id)) {
        return false;
    }
    
    // 2. The index told us the physical page ID. Fetch it through the Buffer Pool.
    Page* data_page = engine->bpm->fetch_page(data_page_id);
    if (!data_page) {
        return false; 
    }
    
    // 3. Extract the value from the loaded data page and copy it to the user's buffer.
    Record* rec = reinterpret_cast<Record*>(data_page->body());
    snprintf(value_out, MAX_VALUE_SIZE, "%s", rec->value);
    
    // 4. Unpin the page cleanly (not dirty since we just read it)
    engine->bpm->unpin_page(data_page_id, false);
    
    return true; // Success!
}

// Prints a quick summary of the database's internal state.
void storage_engine_print_stats(StorageEngine* engine) {
    ::cout << "\n=== Database Statistics ===\n";
    ::cout << "Total pages allocated on disk: " << disk_manager_get_total_pages(engine->disk_manager) << "\n";
    ::cout << "Root node Page ID of the B+ Tree: " << btree_get_root_page_id(engine->index) << "\n";
}
