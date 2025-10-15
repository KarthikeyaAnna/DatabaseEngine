#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <iomanip>
using namespace std;
// ============================================================================
// CONSTANTS & CONFIGURATION
// ============================================================================

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t MAX_KEY_SIZE = 64;
constexpr size_t MAX_VALUE_SIZE = 256;

// B+ tree order (max keys per node)
constexpr size_t BTREE_ORDER = 50;

using PageID = uint32_t;
constexpr PageID INVALID_PAGE_ID = 0xFFFFFFFF;

// ============================================================================
// PAGE STRUCTURE
// ============================================================================

enum class PageType : uint8_t {
    INVALID = 0,
    DATA_PAGE = 1,
    BTREE_INTERNAL = 2,
    BTREE_LEAF = 3
};

// Page Header (32 bytes)
struct PageHeader {
    PageType type;
    uint8_t reserved[3];
    PageID page_id;
    uint32_t record_count;
    uint32_t free_space_offset;
    PageID next_page_id;  // For leaf node linking
    uint8_t padding[12];
} __attribute__((packed));

static_assert(sizeof(PageHeader) == 32, "PageHeader must be 32 bytes");

// Record structure for data pages
struct Record {
    char key[MAX_KEY_SIZE];
    char value[MAX_VALUE_SIZE];
} __attribute__((packed));

// B+ Tree Node Entry
struct BTreeEntry {
    char key[MAX_KEY_SIZE];
    PageID page_id;
} __attribute__((packed));

// Raw page buffer
struct Page {
    char data[PAGE_SIZE];
    
    PageHeader* header() {
        return reinterpret_cast<PageHeader*>(data);
    }
    
    const PageHeader* header() const {
        return reinterpret_cast<const PageHeader*>(data);
    }
    
    char* body() {
        return data + sizeof(PageHeader);
    }
    
    const char* body() const {
        return data + sizeof(PageHeader);
    }
    
    void init(PageType type, PageID id) {
        ::memset(data, 0, PAGE_SIZE);
        PageHeader* h = header();
        h->type = type;
        h->page_id = id;
        h->record_count = 0;
        h->free_space_offset = sizeof(PageHeader);
        h->next_page_id = INVALID_PAGE_ID;
    }
};

// ============================================================================
// DISK MANAGER (C-style struct)
// ============================================================================

struct DiskManager {
    ::fstream* file;
    char* filename;
    PageID next_page_id;
};

// Initialize disk manager
DiskManager* disk_manager_create(const char* filename) {
    DiskManager* dm = (DiskManager*)malloc(sizeof(DiskManager));
    if (!dm) return nullptr;
    
    dm->filename = (char*)malloc(strlen(filename) + 1);
    strcpy(dm->filename, filename);
    
    dm->file = new ::fstream();
    
    // Open or create file
    dm->file->open(filename, ::ios::in | ::ios::out | ::ios::binary);
    if (!dm->file->is_open()) {
        // Create new file
        dm->file->open(filename, ::ios::out | ::ios::binary);
        dm->file->close();
        dm->file->open(filename, ::ios::in | ::ios::out | ::ios::binary);
    }
    
    // Determine next page ID
    dm->file->seekg(0, ::ios::end);
    size_t file_size = dm->file->tellg();
    dm->next_page_id = file_size / PAGE_SIZE;
    
    return dm;
}

// Destroy disk manager
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

// Allocate a new page
PageID disk_manager_allocate_page(DiskManager* dm) {
    return dm->next_page_id++;
}

// Write page to disk
void disk_manager_write_page(DiskManager* dm, PageID page_id, const Page* page) {
    size_t offset = page_id * PAGE_SIZE;
    dm->file->seekp(offset);
    dm->file->write(page->data, PAGE_SIZE);
    dm->file->flush();
}

// Read page from disk
bool disk_manager_read_page(DiskManager* dm, PageID page_id, Page* page) {
    size_t offset = page_id * PAGE_SIZE;
    dm->file->seekg(0, ::ios::end);
    size_t file_size = dm->file->tellg();
    
    if (offset + PAGE_SIZE > file_size) {
        return false;
    }
    
    dm->file->seekg(offset);
    dm->file->read(page->data, PAGE_SIZE);
    return dm->file->gcount() == PAGE_SIZE;
}

PageID disk_manager_get_total_pages(const DiskManager* dm) {
    return dm->next_page_id;
}

// ============================================================================
// B+ TREE INDEX 
// ============================================================================

struct BPlusTree {
    DiskManager* disk_manager;
    PageID root_page_id;
};

// Forward declarations for internal functions
static int btree_compare_keys(const char* key1, const char* key2);
static size_t btree_find_insert_position(const BTreeEntry* entries, size_t count, const char* key);
static PageID btree_split_leaf_node(DiskManager* dm, Page* old_leaf, Page* new_leaf, char* promoted_key);
static PageID btree_split_internal_node(DiskManager* dm, Page* old_internal, Page* new_internal, char* promoted_key);
static bool btree_insert_into_leaf(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                   bool* needs_split, char* promoted_key, PageID* new_page_id);
static bool btree_insert_into_internal(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                       bool* needs_split, char* promoted_key, PageID* new_page_id);

// Create B+ tree
BPlusTree* btree_create(DiskManager* dm) {
    BPlusTree* tree = (BPlusTree*)malloc(sizeof(BPlusTree));
    if (!tree) return nullptr;
    
    tree->disk_manager = dm;
    tree->root_page_id = INVALID_PAGE_ID;
    
    return tree;
}

// Destroy B+ tree
void btree_destroy(BPlusTree* tree) {
    if (tree) {
        free(tree);
    }
}

// Initialize tree with root
void btree_init(BPlusTree* tree) {
    tree->root_page_id = disk_manager_allocate_page(tree->disk_manager);
    Page* root = (Page*)malloc(sizeof(Page));
    root->init(PageType::BTREE_LEAF, tree->root_page_id);
    disk_manager_write_page(tree->disk_manager, tree->root_page_id, root);
    free(root);
}

// Compare keys
static int btree_compare_keys(const char* key1, const char* key2) {
    return strncmp(key1, key2, MAX_KEY_SIZE);
}

// Find position to insert key in sorted array
static size_t btree_find_insert_position(const BTreeEntry* entries, size_t count, const char* key) {
    size_t pos = 0;
    while (pos < count && btree_compare_keys(entries[pos].key, key) < 0) {
        pos++;
    }
    return pos;
}

// Split leaf node
static PageID btree_split_leaf_node(DiskManager* dm, Page* old_leaf, Page* new_leaf, char* promoted_key) {
    PageID new_page_id = disk_manager_allocate_page(dm);
    new_leaf->init(PageType::BTREE_LEAF, new_page_id);
    
    PageHeader* old_h = old_leaf->header();
    PageHeader* new_h = new_leaf->header();
    
    BTreeEntry* old_entries = reinterpret_cast<BTreeEntry*>(old_leaf->body());
    BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_leaf->body());
    
    size_t mid = old_h->record_count / 2;
    
    // Copy second half to new node
    for (size_t i = mid; i < old_h->record_count; i++) {
        new_entries[i - mid] = old_entries[i];
    }
    
    new_h->record_count = old_h->record_count - mid;
    old_h->record_count = mid;
    
    // Update free space
    old_h->free_space_offset = sizeof(PageHeader) + mid * sizeof(BTreeEntry);
    new_h->free_space_offset = sizeof(PageHeader) + new_h->record_count * sizeof(BTreeEntry);
    
    // Link leaf nodes
    new_h->next_page_id = old_h->next_page_id;
    old_h->next_page_id = new_page_id;
    
    // Promoted key is first key of new node
    strncpy(promoted_key, new_entries[0].key, MAX_KEY_SIZE);
    
    return new_page_id;
}

// Split internal node
static PageID btree_split_internal_node(DiskManager* dm, Page* old_internal, Page* new_internal, char* promoted_key) {
    PageID new_page_id = disk_manager_allocate_page(dm);
    new_internal->init(PageType::BTREE_INTERNAL, new_page_id);
    
    PageHeader* old_h = old_internal->header();
    PageHeader* new_h = new_internal->header();
    
    BTreeEntry* old_entries = reinterpret_cast<BTreeEntry*>(old_internal->body());
    BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_internal->body());
    
    size_t mid = old_h->record_count / 2;
    
    // Copy promoted key
    strncpy(promoted_key, old_entries[mid].key, MAX_KEY_SIZE);
    
    // Copy entries after mid to new node
    for (size_t i = mid + 1; i < old_h->record_count; i++) {
        new_entries[i - mid - 1] = old_entries[i];
    }
    
    new_h->record_count = old_h->record_count - mid - 1;
    old_h->record_count = mid;
    
    old_h->free_space_offset = sizeof(PageHeader) + mid * sizeof(BTreeEntry);
    new_h->free_space_offset = sizeof(PageHeader) + new_h->record_count * sizeof(BTreeEntry);
    
    return new_page_id;
}

// Insert into leaf node
static bool btree_insert_into_leaf(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                   bool* needs_split, char* promoted_key, PageID* new_page_id) {
    Page* page = (Page*)malloc(sizeof(Page));
    if (!disk_manager_read_page(tree->disk_manager, page_id, page)) {
        free(page);
        return false;
    }
    
    PageHeader* h = page->header();
    BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
    
    // Check if key already exists
    for (size_t i = 0; i < h->record_count; i++) {
        if (btree_compare_keys(entries[i].key, key) == 0) {
            free(page);
            return false; // Duplicate key
        }
    }
    
    // Check if split needed
    if (h->record_count >= BTREE_ORDER) {
        *needs_split = true;
        
        // Create new leaf for split
        Page* new_leaf = (Page*)malloc(sizeof(Page));
        *new_page_id = btree_split_leaf_node(tree->disk_manager, page, new_leaf, promoted_key);
        
        // Insert into appropriate leaf
        if (btree_compare_keys(key, promoted_key) < 0) {
            // Insert into old leaf
            h = page->header();
            entries = reinterpret_cast<BTreeEntry*>(page->body());
            size_t pos = btree_find_insert_position(entries, h->record_count, key);
            
            for (size_t i = h->record_count; i > pos; i--) {
                entries[i] = entries[i-1];
            }
            
            strncpy(entries[pos].key, key, MAX_KEY_SIZE);
            entries[pos].page_id = data_page_id;
            h->record_count++;
            
            disk_manager_write_page(tree->disk_manager, page_id, page);
        } else {
            // Insert into new leaf
            PageHeader* new_h = new_leaf->header();
            BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_leaf->body());
            size_t pos = btree_find_insert_position(new_entries, new_h->record_count, key);
            
            for (size_t i = new_h->record_count; i > pos; i--) {
                new_entries[i] = new_entries[i-1];
            }
            
            strncpy(new_entries[pos].key, key, MAX_KEY_SIZE);
            new_entries[pos].page_id = data_page_id;
            new_h->record_count++;
        }
        
        disk_manager_write_page(tree->disk_manager, *new_page_id, new_leaf);
        free(new_leaf);
    } else {
        // No split needed, simple insert
        *needs_split = false;
        size_t pos = btree_find_insert_position(entries, h->record_count, key);
        
        for (size_t i = h->record_count; i > pos; i--) {
            entries[i] = entries[i-1];
        }
        
        strncpy(entries[pos].key, key, MAX_KEY_SIZE);
        entries[pos].page_id = data_page_id;
        h->record_count++;
        h->free_space_offset += sizeof(BTreeEntry);
    }
    
    disk_manager_write_page(tree->disk_manager, page_id, page);
    free(page);
    return true;
}

// Insert into internal node
static bool btree_insert_into_internal(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                       bool* needs_split, char* promoted_key, PageID* new_page_id) {
    Page* page = (Page*)malloc(sizeof(Page));
    if (!disk_manager_read_page(tree->disk_manager, page_id, page)) {
        free(page);
        return false;
    }
    
    PageHeader* h = page->header();
    BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
    
    // Find child to descend
    size_t i = 0;
    while (i < h->record_count && btree_compare_keys(key, entries[i].key) >= 0) {
        i++;
    }
    
    PageID child_id = (i == 0 && h->record_count > 0) ? entries[0].page_id : 
                      (i > 0 ? entries[i-1].page_id : INVALID_PAGE_ID);
    
    if (child_id == INVALID_PAGE_ID) {
        free(page);
        return false;
    }
    
    // Read child to determine type
    Page* child_page = (Page*)malloc(sizeof(Page));
    if (!disk_manager_read_page(tree->disk_manager, child_id, child_page)) {
        free(child_page);
        free(page);
        return false;
    }
    
    bool child_needs_split = false;
    char child_promoted_key[MAX_KEY_SIZE];
    PageID child_new_page_id = INVALID_PAGE_ID;
    
    // Recursively insert
    bool success = false;
    if (child_page->header()->type == PageType::BTREE_LEAF) {
        success = btree_insert_into_leaf(tree, child_id, key, data_page_id, 
                                        &child_needs_split, child_promoted_key, &child_new_page_id);
    } else {
        success = btree_insert_into_internal(tree, child_id, key, data_page_id,
                                            &child_needs_split, child_promoted_key, &child_new_page_id);
    }
    
    free(child_page);
    
    if (!success) {
        free(page);
        return false;
    }
    
    // Handle child split
    if (child_needs_split) {
        // Need to insert promoted key and new page ID
        if (h->record_count >= BTREE_ORDER) {
            // This node also needs to split
            *needs_split = true;
            
            // First insert the child's promoted key
            size_t pos = btree_find_insert_position(entries, h->record_count, child_promoted_key);
            for (size_t j = h->record_count; j > pos; j--) {
                entries[j] = entries[j-1];
            }
            strncpy(entries[pos].key, child_promoted_key, MAX_KEY_SIZE);
            entries[pos].page_id = child_new_page_id;
            h->record_count++;
            
            disk_manager_write_page(tree->disk_manager, page_id, page);
            
            // Now split this internal node
            Page* new_internal = (Page*)malloc(sizeof(Page));
            *new_page_id = btree_split_internal_node(tree->disk_manager, page, new_internal, promoted_key);
            disk_manager_write_page(tree->disk_manager, *new_page_id, new_internal);
            disk_manager_write_page(tree->disk_manager, page_id, page);
            free(new_internal);
        } else {
            // Insert promoted key without split
            *needs_split = false;
            size_t pos = btree_find_insert_position(entries, h->record_count, child_promoted_key);
            
            for (size_t j = h->record_count; j > pos; j--) {
                entries[j] = entries[j-1];
            }
            
            strncpy(entries[pos].key, child_promoted_key, MAX_KEY_SIZE);
            entries[pos].page_id = child_new_page_id;
            h->record_count++;
            
            disk_manager_write_page(tree->disk_manager, page_id, page);
        }
    }
    
    free(page);
    return true;
}

// Insert key-value mapping
bool btree_insert(BPlusTree* tree, const char* key, PageID data_page_id) {
    if (tree->root_page_id == INVALID_PAGE_ID) {
        btree_init(tree);
    }
    
    Page* root = (Page*)malloc(sizeof(Page));
    if (!disk_manager_read_page(tree->disk_manager, tree->root_page_id, root)) {
        free(root);
        return false;
    }
    
    bool needs_split = false;
    char promoted_key[MAX_KEY_SIZE];
    PageID new_page_id = INVALID_PAGE_ID;
    
    bool success = false;
    if (root->header()->type == PageType::BTREE_LEAF) {
        success = btree_insert_into_leaf(tree, tree->root_page_id, key, data_page_id,
                                        &needs_split, promoted_key, &new_page_id);
    } else {
        success = btree_insert_into_internal(tree, tree->root_page_id, key, data_page_id,
                                            &needs_split, promoted_key, &new_page_id);
    }
    
    free(root);
    
    if (!success) return false;
    
    // Handle root split
    if (needs_split) {
        PageID new_root_id = disk_manager_allocate_page(tree->disk_manager);
        Page* new_root = (Page*)malloc(sizeof(Page));
        new_root->init(PageType::BTREE_INTERNAL, new_root_id);
        
        PageHeader* new_root_h = new_root->header();
        BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(new_root->body());
        
        // First child is old root
        strncpy(entries[0].key, promoted_key, MAX_KEY_SIZE);
        entries[0].page_id = tree->root_page_id;
        
        // Second entry points to new page
        strncpy(entries[1].key, promoted_key, MAX_KEY_SIZE);
        entries[1].page_id = new_page_id;
        
        new_root_h->record_count = 2;
        
        disk_manager_write_page(tree->disk_manager, new_root_id, new_root);
        tree->root_page_id = new_root_id;
        free(new_root);
    }
    
    return true;
}

// Search for key
bool btree_search(BPlusTree* tree, const char* key, PageID* data_page_id) {
    if (tree->root_page_id == INVALID_PAGE_ID) {
        return false;
    }
    
    PageID current_id = tree->root_page_id;
    
    while (true) {
        Page* page = (Page*)malloc(sizeof(Page));
        if (!disk_manager_read_page(tree->disk_manager, current_id, page)) {
            free(page);
            return false;
        }
        
        PageHeader* h = page->header();
        
        if (h->type == PageType::BTREE_LEAF) {
            // Search in leaf
            BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
            for (size_t i = 0; i < h->record_count; i++) {
                if (btree_compare_keys(entries[i].key, key) == 0) {
                    *data_page_id = entries[i].page_id;
                    free(page);
                    return true;
                }
            }
            free(page);
            return false;
        } else {
            // Navigate internal node
            BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
            size_t i = 0;
            while (i < h->record_count && btree_compare_keys(key, entries[i].key) >= 0) {
                i++;
            }
            
            current_id = (i == 0 && h->record_count > 0) ? entries[0].page_id :
                        (i > 0 ? entries[i-1].page_id : INVALID_PAGE_ID);
            
            free(page);
            
            if (current_id == INVALID_PAGE_ID) {
                return false;
            }
        }
    }
}

PageID btree_get_root_page_id(const BPlusTree* tree) {
    return tree->root_page_id;
}

// ============================================================================
// STORAGE ENGINE (C-style struct)
// ============================================================================

struct StorageEngine {
    DiskManager* disk_manager;
    BPlusTree* index;
};

// Create storage engine
StorageEngine* storage_engine_create(const char* db_file) {
    StorageEngine* engine = (StorageEngine*)malloc(sizeof(StorageEngine));
    if (!engine) return nullptr;
    
    engine->disk_manager = disk_manager_create(db_file);
    if (!engine->disk_manager) {
        free(engine);
        return nullptr;
    }
    
    engine->index = btree_create(engine->disk_manager);
    if (!engine->index) {
        disk_manager_destroy(engine->disk_manager);
        free(engine);
        return nullptr;
    }
    
    return engine;
}

// Destroy storage engine
void storage_engine_destroy(StorageEngine* engine) {
    if (!engine) return;
    
    if (engine->index) {
        btree_destroy(engine->index);
    }
    
    if (engine->disk_manager) {
        disk_manager_destroy(engine->disk_manager);
    }
    
    free(engine);
}

// Insert record
bool storage_engine_insert(StorageEngine* engine, const char* key, const char* value) {
    // Create data page
    PageID data_page_id = disk_manager_allocate_page(engine->disk_manager);
    Page* data_page = (Page*)malloc(sizeof(Page));
    data_page->init(PageType::DATA_PAGE, data_page_id);
    
    // Write record
    Record* rec = reinterpret_cast<Record*>(data_page->body());
    strncpy(rec->key, key, MAX_KEY_SIZE);
    strncpy(rec->value, value, MAX_VALUE_SIZE);
    
    PageHeader* h = data_page->header();
    h->record_count = 1;
    h->free_space_offset = sizeof(PageHeader) + sizeof(Record);
    
    disk_manager_write_page(engine->disk_manager, data_page_id, data_page);
    free(data_page);
    
    // Insert into index
    return btree_insert(engine->index, key, data_page_id);
}

// Search for record
bool storage_engine_search(StorageEngine* engine, const char* key, char* value_out) {
    PageID data_page_id;
    if (!btree_search(engine->index, key, &data_page_id)) {
        return false;
    }
    
    // Read data page
    Page* data_page = (Page*)malloc(sizeof(Page));
    if (!disk_manager_read_page(engine->disk_manager, data_page_id, data_page)) {
        free(data_page);
        return false;
    }
    
    Record* rec = reinterpret_cast<Record*>(data_page->body());
    strncpy(value_out, rec->value, MAX_VALUE_SIZE);
    free(data_page);
    return true;
}

// Print statistics
void storage_engine_print_stats(StorageEngine* engine) {
    ::cout << "\n=== Database Statistics ===\n";
    ::cout << "Total pages: " << disk_manager_get_total_pages(engine->disk_manager) << "\n";
    ::cout << "Root page ID: " << btree_get_root_page_id(engine->index) << "\n";
}

// ============================================================================
// MAIN 
// ============================================================================

int main() {
    ::cout << "=== Custom Database Engine Demo (C-Style with malloc) ===\n\n";
    
    // Create storage engine
    StorageEngine* db = storage_engine_create("test.db");
    if (!db) {
        ::cerr << "Failed to create storage engine\n";
        return 1;
    }
    
    // Insert test records
    ::cout << "Inserting records...\n";
    
    struct TestRecord {
        const char* key;
        const char* value;
    };
    
    TestRecord records[] = {
        {"user:1001", "Alice Johnson, Software Engineer"},
        {"user:1002", "Bob Smith, Database Administrator"},
        {"user:1003", "Charlie Davis, Systems Architect"},
        {"user:1004", "Diana Prince, DevOps Engineer"},
        {"user:1005", "Eve Martinez, Backend Developer"},
        {"product:2001", "Laptop, $1299.99"},
        {"product:2002", "Mouse, $29.99"},
        {"product:2003", "Keyboard, $79.99"},
        {"order:3001", "Order #3001, Total: $1409.97"},
        {"order:3002", "Order #3002, Total: $109.98"}
    };
    
    for (const auto& rec : records) {
        if (storage_engine_insert(db, rec.key, rec.value)) {
            ::cout << "  ✓ Inserted: " << rec.key << "\n";
        } else {
            ::cout << "  ✗ Failed: " << rec.key << "\n";
        }
    }
    
    // Search test
    ::cout << "\n=== Search Operations ===\n";
    
    const char* search_keys[] = {
        "user:1002",
        "product:2001",
        "order:3002",
        "user:9999"  // Non-existent
    };
    
    char value[MAX_VALUE_SIZE];
    for (const char* key : search_keys) {
        if (storage_engine_search(db, key, value)) {
            ::cout << "  Found [" << key << "]: " << value << "\n";
        } else {
            ::cout << "  Not found: " << key << "\n";
        }
    }
    
    storage_engine_print_stats(db);
    

    return 0;
}