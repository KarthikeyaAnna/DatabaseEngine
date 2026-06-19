#include "BPlusTree.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

static int btree_compare_keys(const char* key1, const char* key2) {
    return strncmp(key1, key2, MAX_KEY_SIZE);
}

static size_t btree_find_insert_position(const BTreeEntry* entries, size_t count, const char* key) {
    size_t pos = 0;
    while (pos < count && btree_compare_keys(entries[pos].key, key) < 0) {
        pos++;
    }
    return pos;
}

static void btree_split_leaf_node(Page* old_leaf, Page* new_leaf, char* promoted_key) {
    PageHeader* old_h = old_leaf->header();
    PageHeader* new_h = new_leaf->header();
    
    BTreeEntry* old_entries = reinterpret_cast<BTreeEntry*>(old_leaf->body());
    BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_leaf->body());
    
    size_t mid = old_h->record_count / 2;
    
    for (size_t i = mid; i < old_h->record_count; i++) {
        new_entries[i - mid] = old_entries[i];
    }
    
    new_h->record_count = old_h->record_count - mid;
    old_h->record_count = mid;
    
    old_h->free_space_offset = sizeof(PageHeader) + mid * sizeof(BTreeEntry);
    new_h->free_space_offset = sizeof(PageHeader) + new_h->record_count * sizeof(BTreeEntry);
    
    new_h->next_page_id = old_h->next_page_id;
    old_h->next_page_id = new_h->page_id;
    
    snprintf(promoted_key, MAX_KEY_SIZE, "%s", new_entries[0].key);
}

static void btree_split_internal_node(Page* old_internal, Page* new_internal, char* promoted_key) {
    PageHeader* old_h = old_internal->header();
    PageHeader* new_h = new_internal->header();
    
    BTreeEntry* old_entries = reinterpret_cast<BTreeEntry*>(old_internal->body());
    BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_internal->body());
    
    size_t mid = old_h->record_count / 2;
    
    snprintf(promoted_key, MAX_KEY_SIZE, "%s", old_entries[mid].key);
    
    for (size_t i = mid + 1; i < old_h->record_count; i++) {
        new_entries[i - mid - 1] = old_entries[i];
    }
    
    new_h->record_count = old_h->record_count - mid - 1;
    old_h->record_count = mid;
    
    old_h->free_space_offset = sizeof(PageHeader) + mid * sizeof(BTreeEntry);
    new_h->free_space_offset = sizeof(PageHeader) + new_h->record_count * sizeof(BTreeEntry);
}

static bool btree_insert_into_leaf(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                   bool* needs_split, char* promoted_key, PageID* new_page_id_out) {
    Page* page = tree->bpm->fetch_page(page_id);
    if (!page) return false;
    
    PageHeader* h = page->header();
    BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
    
    for (size_t i = 0; i < h->record_count; i++) {
        if (btree_compare_keys(entries[i].key, key) == 0) {
            tree->bpm->unpin_page(page_id, false);
            return false;
        }
    }
    
    if (h->record_count >= BTREE_ORDER) {
        *needs_split = true;
        
        PageID new_page_id;
        Page* new_leaf = tree->bpm->new_page(&new_page_id);
        new_leaf->init(PageType::BTREE_LEAF, new_page_id);
        
        btree_split_leaf_node(page, new_leaf, promoted_key);
        
        if (btree_compare_keys(key, promoted_key) < 0) {
            size_t pos = btree_find_insert_position(entries, h->record_count, key);
            for (size_t i = h->record_count; i > pos; i--) entries[i] = entries[i-1];
            snprintf(entries[pos].key, MAX_KEY_SIZE, "%s", key);
            entries[pos].page_id = data_page_id;
            h->record_count++;
        } else {
            PageHeader* new_h = new_leaf->header();
            BTreeEntry* new_entries = reinterpret_cast<BTreeEntry*>(new_leaf->body());
            size_t pos = btree_find_insert_position(new_entries, new_h->record_count, key);
            for (size_t i = new_h->record_count; i > pos; i--) new_entries[i] = new_entries[i-1];
            snprintf(new_entries[pos].key, MAX_KEY_SIZE, "%s", key);
            new_entries[pos].page_id = data_page_id;
            new_h->record_count++;
        }
        
        *new_page_id_out = new_page_id;
        tree->bpm->unpin_page(new_page_id, true);
    } else {
        *needs_split = false;
        size_t pos = btree_find_insert_position(entries, h->record_count, key);
        for (size_t i = h->record_count; i > pos; i--) entries[i] = entries[i-1];
        snprintf(entries[pos].key, MAX_KEY_SIZE, "%s", key);
        entries[pos].page_id = data_page_id;
        h->record_count++;
        h->free_space_offset += sizeof(BTreeEntry);
    }
    
    tree->bpm->unpin_page(page_id, true);
    return true;
}

static bool btree_insert_into_internal(BPlusTree* tree, PageID page_id, const char* key, PageID data_page_id,
                                       bool* needs_split, char* promoted_key, PageID* new_page_id_out) {
    Page* page = tree->bpm->fetch_page(page_id);
    if (!page) return false;
    
    PageHeader* h = page->header();
    BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
    
    size_t i = 0;
    while (i < h->record_count && btree_compare_keys(key, entries[i].key) >= 0) i++;
    
    PageID child_id = (i == 0 && h->record_count > 0) ? entries[0].page_id : 
                      (i > 0 ? entries[i-1].page_id : INVALID_PAGE_ID);
    
    if (child_id == INVALID_PAGE_ID) {
        tree->bpm->unpin_page(page_id, false);
        return false;
    }
    
    Page* child_page = tree->bpm->fetch_page(child_id);
    if (!child_page) {
        tree->bpm->unpin_page(page_id, false);
        return false;
    }
    
    bool is_leaf = (child_page->header()->type == PageType::BTREE_LEAF);
    tree->bpm->unpin_page(child_id, false); // Just peaked to see the type
    
    bool child_needs_split = false;
    char child_promoted_key[MAX_KEY_SIZE];
    PageID child_new_page_id = INVALID_PAGE_ID;
    
    bool success = false;
    if (is_leaf) {
        success = btree_insert_into_leaf(tree, child_id, key, data_page_id, 
                                        &child_needs_split, child_promoted_key, &child_new_page_id);
    } else {
        success = btree_insert_into_internal(tree, child_id, key, data_page_id,
                                            &child_needs_split, child_promoted_key, &child_new_page_id);
    }
    
    if (!success) {
        tree->bpm->unpin_page(page_id, false);
        return false;
    }
    
    if (child_needs_split) {
        if (h->record_count >= BTREE_ORDER) {
            *needs_split = true;
            
            size_t pos = btree_find_insert_position(entries, h->record_count, child_promoted_key);
            for (size_t j = h->record_count; j > pos; j--) entries[j] = entries[j-1];
            snprintf(entries[pos].key, MAX_KEY_SIZE, "%s", child_promoted_key);
            entries[pos].page_id = child_new_page_id;
            h->record_count++;
            
            PageID new_internal_id;
            Page* new_internal = tree->bpm->new_page(&new_internal_id);
            new_internal->init(PageType::BTREE_INTERNAL, new_internal_id);
            
            btree_split_internal_node(page, new_internal, promoted_key);
            
            *new_page_id_out = new_internal_id;
            tree->bpm->unpin_page(new_internal_id, true);
        } else {
            *needs_split = false;
            size_t pos = btree_find_insert_position(entries, h->record_count, child_promoted_key);
            for (size_t j = h->record_count; j > pos; j--) entries[j] = entries[j-1];
            snprintf(entries[pos].key, MAX_KEY_SIZE, "%s", child_promoted_key);
            entries[pos].page_id = child_new_page_id;
            h->record_count++;
        }
        tree->bpm->unpin_page(page_id, true);
    } else {
        tree->bpm->unpin_page(page_id, false);
    }
    
    return true;
}

BPlusTree* btree_create(BufferPoolManager* bpm) {
    BPlusTree* tree = new BPlusTree();
    tree->bpm = bpm;
    tree->root_page_id = INVALID_PAGE_ID;
    return tree;
}

void btree_destroy(BPlusTree* tree) {
    if (tree) delete tree;
}

void btree_init(BPlusTree* tree) {
    Page* root = tree->bpm->new_page(&tree->root_page_id);
    root->init(PageType::BTREE_LEAF, tree->root_page_id);
    tree->bpm->unpin_page(tree->root_page_id, true);
}

bool btree_insert(BPlusTree* tree, const char* key, PageID data_page_id) {
    if (tree->root_page_id == INVALID_PAGE_ID) {
        btree_init(tree);
    }
    
    Page* root = tree->bpm->fetch_page(tree->root_page_id);
    if (!root) return false;
    
    bool needs_split = false;
    char promoted_key[MAX_KEY_SIZE];
    PageID new_page_id = INVALID_PAGE_ID;
    bool is_leaf = (root->header()->type == PageType::BTREE_LEAF);
    tree->bpm->unpin_page(tree->root_page_id, false);
    
    bool success = false;
    if (is_leaf) {
        success = btree_insert_into_leaf(tree, tree->root_page_id, key, data_page_id,
                                        &needs_split, promoted_key, &new_page_id);
    } else {
        success = btree_insert_into_internal(tree, tree->root_page_id, key, data_page_id,
                                            &needs_split, promoted_key, &new_page_id);
    }
    
    if (!success) return false;
    
    if (needs_split) {
        PageID new_root_id;
        Page* new_root = tree->bpm->new_page(&new_root_id);
        new_root->init(PageType::BTREE_INTERNAL, new_root_id);
        
        PageHeader* new_root_h = new_root->header();
        BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(new_root->body());
        
        snprintf(entries[0].key, MAX_KEY_SIZE, "%s", promoted_key);
        entries[0].page_id = tree->root_page_id;
        
        snprintf(entries[1].key, MAX_KEY_SIZE, "%s", promoted_key);
        entries[1].page_id = new_page_id;
        
        new_root_h->record_count = 2;
        tree->root_page_id = new_root_id;
        
        tree->bpm->unpin_page(new_root_id, true);
    }
    
    return true;
}

bool btree_search(BPlusTree* tree, const char* key, PageID* data_page_id) {
    if (tree->root_page_id == INVALID_PAGE_ID) return false;
    
    PageID current_id = tree->root_page_id;
    
    while (true) {
        Page* page = tree->bpm->fetch_page(current_id);
        if (!page) return false;
        
        PageHeader* h = page->header();
        BTreeEntry* entries = reinterpret_cast<BTreeEntry*>(page->body());
        
        if (h->type == PageType::BTREE_LEAF) {
            for (size_t i = 0; i < h->record_count; i++) {
                if (btree_compare_keys(entries[i].key, key) == 0) {
                    *data_page_id = entries[i].page_id;
                    tree->bpm->unpin_page(current_id, false);
                    return true;
                }
            }
            tree->bpm->unpin_page(current_id, false);
            return false;
        } else {
            size_t i = 0;
            while (i < h->record_count && btree_compare_keys(key, entries[i].key) >= 0) i++;
            
            PageID next_id = (i == 0 && h->record_count > 0) ? entries[0].page_id :
                        (i > 0 ? entries[i-1].page_id : INVALID_PAGE_ID);
            
            tree->bpm->unpin_page(current_id, false);
            current_id = next_id;
            
            if (current_id == INVALID_PAGE_ID) return false;
        }
    }
}

PageID btree_get_root_page_id(const BPlusTree* tree) {
    return tree->root_page_id;
}
