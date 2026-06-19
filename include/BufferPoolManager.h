#pragma once

#include "DiskManager.h"
#include "Page.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>

/**
 * ============================================================================
 * BUFFER POOL MANAGER
 * ============================================================================
 * The Buffer Pool Manager caches pages in memory so we don't have to hit the 
 * disk for every single read or write. This is critical for performance and 
 * true database concurrency.
 */
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    // Fetches a page from the buffer pool. If it's not in memory, reads it from disk.
    // The page is "pinned" meaning it cannot be evicted until unpinned.
    Page* fetch_page(PageID page_id);

    // Unpins a page. If is_dirty is true, it marks the page to be written to disk 
    // before it gets evicted.
    bool unpin_page(PageID page_id, bool is_dirty);

    // Creates a brand new page on disk and returns its pinned memory frame.
    Page* new_page(PageID* page_id);

    // Flushes a specific page to disk immediately.
    bool flush_page(PageID page_id);

    // Flushes all dirty pages to disk (used on shutdown).
    void flush_all_pages();

private:
    size_t pool_size_;
    DiskManager* disk_manager_;
    std::vector<Page*> pages_;
    
    // Page table: Maps PageID to the index in the pages_ array (frame ID)
    std::unordered_map<PageID, size_t> page_table_;
    
    // LRU state (Least Recently Used)
    std::list<size_t> replacer_;
    std::unordered_map<size_t, std::list<size_t>::iterator> replacer_map_;

    std::vector<int> pin_counts_;
    std::vector<bool> is_dirty_;

    std::mutex latch_;

    // Internal helper to find an unpinned frame to evict
    bool find_victim(size_t* frame_id);
};
