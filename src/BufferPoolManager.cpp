#include "BufferPoolManager.h"

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager) 
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    pages_.resize(pool_size_);
    pin_counts_.resize(pool_size_, 0);
    is_dirty_.resize(pool_size_, false);
    
    for (size_t i = 0; i < pool_size_; i++) {
        pages_[i] = new Page();
        replacer_.push_back(i); // Initially all frames are free
        replacer_map_[i] = std::prev(replacer_.end());
    }
}

BufferPoolManager::~BufferPoolManager() {
    flush_all_pages();
    for (size_t i = 0; i < pool_size_; i++) {
        delete pages_[i];
    }
}

Page* BufferPoolManager::fetch_page(PageID page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    // 1. If it's already in memory, just return it
    if (page_table_.find(page_id) != page_table_.end()) {
        size_t frame_id = page_table_[page_id];
        pin_counts_[frame_id]++;
        
        // Remove from LRU replacer since it's now pinned
        if (replacer_map_.find(frame_id) != replacer_map_.end()) {
            replacer_.erase(replacer_map_[frame_id]);
            replacer_map_.erase(frame_id);
        }
        return pages_[frame_id];
    }

    // 2. Not in memory. Find a victim frame to evict.
    size_t frame_id;
    if (!find_victim(&frame_id)) {
        return nullptr; // No unpinned pages available! (Buffer pool is full)
    }

    // 3. If the victim is dirty, flush it to disk first
    if (is_dirty_[frame_id]) {
        PageID old_page_id = INVALID_PAGE_ID;
        for (const auto& pair : page_table_) {
            if (pair.second == frame_id) {
                old_page_id = pair.first;
                break;
            }
        }
        if (old_page_id != INVALID_PAGE_ID) {
            disk_manager_write_page(disk_manager_, old_page_id, pages_[frame_id]);
            page_table_.erase(old_page_id);
        }
    } else {
        // Just remove from page table
        PageID old_page_id = INVALID_PAGE_ID;
        for (const auto& pair : page_table_) {
            if (pair.second == frame_id) {
                old_page_id = pair.first;
                break;
            }
        }
        if (old_page_id != INVALID_PAGE_ID) page_table_.erase(old_page_id);
    }

    // 4. Read the new page from disk into the victim frame
    page_table_[page_id] = frame_id;
    pin_counts_[frame_id] = 1;
    is_dirty_[frame_id] = false;
    disk_manager_read_page(disk_manager_, page_id, pages_[frame_id]);

    return pages_[frame_id];
}

bool BufferPoolManager::unpin_page(PageID page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    if (page_table_.find(page_id) == page_table_.end()) return false;
    
    size_t frame_id = page_table_[page_id];
    if (pin_counts_[frame_id] <= 0) return false;

    if (is_dirty) is_dirty_[frame_id] = true;
    
    pin_counts_[frame_id]--;
    
    // If no one is using it anymore, add it to the LRU replacer list
    if (pin_counts_[frame_id] == 0) {
        replacer_.push_back(frame_id);
        replacer_map_[frame_id] = std::prev(replacer_.end());
    }
    return true;
}

Page* BufferPoolManager::new_page(PageID* page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    size_t frame_id;
    if (!find_victim(&frame_id)) return nullptr;

    // Allocate on disk
    *page_id = disk_manager_allocate_page(disk_manager_);

    // Evict old page if dirty
    if (is_dirty_[frame_id]) {
        PageID old_page_id = INVALID_PAGE_ID;
        for (const auto& pair : page_table_) {
            if (pair.second == frame_id) { old_page_id = pair.first; break; }
        }
        if (old_page_id != INVALID_PAGE_ID) {
            disk_manager_write_page(disk_manager_, old_page_id, pages_[frame_id]);
            page_table_.erase(old_page_id);
        }
    } else {
        PageID old_page_id = INVALID_PAGE_ID;
        for (const auto& pair : page_table_) {
            if (pair.second == frame_id) { old_page_id = pair.first; break; }
        }
        if (old_page_id != INVALID_PAGE_ID) page_table_.erase(old_page_id);
    }

    page_table_[*page_id] = frame_id;
    pin_counts_[frame_id] = 1;
    is_dirty_[frame_id] = false;
    
    pages_[frame_id]->init(PageType::INVALID, *page_id);
    
    return pages_[frame_id];
}

bool BufferPoolManager::flush_page(PageID page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (page_table_.find(page_id) == page_table_.end()) return false;
    size_t frame_id = page_table_[page_id];
    disk_manager_write_page(disk_manager_, page_id, pages_[frame_id]);
    is_dirty_[frame_id] = false;
    return true;
}

void BufferPoolManager::flush_all_pages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (const auto& pair : page_table_) {
        PageID page_id = pair.first;
        size_t frame_id = pair.second;
        if (is_dirty_[frame_id]) {
            disk_manager_write_page(disk_manager_, page_id, pages_[frame_id]);
            is_dirty_[frame_id] = false;
        }
    }
}

bool BufferPoolManager::find_victim(size_t* frame_id) {
    if (replacer_.empty()) return false;
    *frame_id = replacer_.front();
    replacer_.pop_front();
    replacer_map_.erase(*frame_id);
    return true;
}
