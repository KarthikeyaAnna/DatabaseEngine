# Custom Database Engine with Paging & B+ Tree Indexing

A lightweight, disk-backed storage engine implemented in C++17 demonstrating real database internals concepts including page-based storage, B+ tree indexing, and persistent data structures.

## 🎯 Project Overview

This project implements a **production-quality database storage engine** from scratch, showcasing:
- Fixed-size page-based disk storage (4KB pages)
- Disk-backed B+ tree index for efficient lookups
- Clear separation between in-memory logic and on-disk structures
- Single-file database with sequential page allocation
- Insert and search operations with O(log n) complexity

**Perfect for**: Systems programming portfolios, database interview prep, understanding DBMS internals

---

## 🏗️ Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                     Storage Engine                          │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                  B+ Tree Index                       │   │
│  │    (In-memory traversal + disk-backed nodes)        │   │
│  └─────────────────┬───────────────────────────────────┘   │
│                    │                                         │
│  ┌─────────────────┴───────────────────────────────────┐   │
│  │              Disk Manager                            │   │
│  │   (Page allocation, read/write operations)          │   │
│  └─────────────────┬───────────────────────────────────┘   │
│                    │                                         │
│  ┌─────────────────┴───────────────────────────────────┐   │
│  │           Disk Storage (test.db)                     │   │
│  │   [Page 0][Page 1][Page 2]...[Page N]               │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Module Breakdown

1. **Page** (`struct Page`)
   - Fixed 4096-byte buffer
   - Type-agnostic container for any page type
   - Provides header/body accessors

2. **DiskManager** (`class DiskManager`)
   - Manages single database file
   - Allocates new pages sequentially
   - Performs page-level I/O operations
   - Tracks next available page ID

3. **B+ Tree** (`class BPlusTree`)
   - Implements disk-backed B+ tree index
   - Internal nodes store keys → child page IDs
   - Leaf nodes store keys → data page IDs
   - Handles node splits and root promotion
   - Maintains sorted order for efficient search

4. **StorageEngine** (`class StorageEngine`)
   - High-level API for insert/search
   - Coordinates between index and data pages
   - Manages record storage

---

## 📐 Page Layout Design

### Data Page Structure
```
┌────────────────────────────────────────────────────┐
│  Page Header (32 bytes)                            │
│  ┌──────────────────────────────────────────────┐ │
│  │ type: DATA_PAGE                              │ │
│  │ page_id: 5                                   │ │
│  │ record_count: 1                              │ │
│  │ free_space_offset: 352                       │ │
│  │ next_page_id: INVALID                        │ │
│  └──────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│  Record Data (320 bytes)                           │
│  ┌──────────────────────────────────────────────┐ │
│  │ key[64]: "user:1001"                         │ │
│  │ value[256]: "Alice Johnson, Engineer"        │ │
│  └──────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│  Free Space (3744 bytes)                           │
└────────────────────────────────────────────────────┘
Total: 4096 bytes
```

### B+ Tree Leaf Page Structure
```
┌────────────────────────────────────────────────────┐
│  Page Header (32 bytes)                            │
│  ┌──────────────────────────────────────────────┐ │
│  │ type: BTREE_LEAF                             │ │
│  │ page_id: 2                                   │ │
│  │ record_count: 3                              │ │
│  │ next_page_id: 7  (next leaf for range scan) │ │
│  └──────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│  Entries (68 bytes each)                           │
│  ┌──────────────────────────────────────────────┐ │
│  │ Entry 0: "order:3001" → Data Page 10        │ │
│  │ Entry 1: "product:2001" → Data Page 11      │ │
│  │ Entry 2: "user:1001" → Data Page 12          │ │
│  └──────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│  Free Space                                        │
└────────────────────────────────────────────────────┘
Max entries per leaf: 50 (B+ tree order)
```

### B+ Tree Internal Page Structure
```
┌────────────────────────────────────────────────────┐
│  Page Header (32 bytes)                            │
│  ┌──────────────────────────────────────────────┐ │
│  │ type: BTREE_INTERNAL                         │ │
│  │ page_id: 1                                   │ │
│  │ record_count: 2                              │ │
│  └──────────────────────────────────────────────┘ │
├────────────────────────────────────────────────────┤
│  Entries (key → child page ID)                     │
│  ┌──────────────────────────────────────────────┐ │
│  │ Entry 0: "product:2000" → Page 2 (leaf)     │ │
│  │ Entry 1: "user:1000" → Page 3 (leaf)        │ │
│  └──────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────┘
Internal nodes guide search to appropriate leaf
```

---

## 🔄 Insert Operation Flow

### Step-by-Step Insert Process

```
INSERT("user:1005", "Eve Martinez, Backend Developer")
│
├─ 1. Allocate Data Page
│     ├─ Request new page ID from DiskManager → Page 15
│     ├─ Initialize page header (type: DATA_PAGE)
│     └─ Write record to page body
│
├─ 2. Write Data Page to Disk
│     └─ DiskManager writes page at offset (15 * 4096)
│
├─ 3. Index Insertion (B+ Tree)
│     ├─ Start at root page (Page 1)
│     ├─ Traverse internal nodes following keys
│     │    └─ "user:1005" ≥ "user:1000" → follow right pointer
│     │
│     ├─ Reach leaf page (Page 3)
│     ├─ Check capacity: current=48, max=50 → OK
│     ├─ Find insertion position (binary search)
│     ├─ Shift entries to make space
│     └─ Insert: "user:1005" → Page 15
│
└─ 4. Write Updated Leaf to Disk
      └─ Persist modified leaf page
```

### Insert with Node Split

```
INSERT causing split (when leaf is full):
│
├─ Leaf node has 50 entries (at capacity)
│
├─ Split Operation:
│   ├─ Allocate new leaf page (Page 16)
│   ├─ Move entries [25-49] to new page
│   ├─ Keep entries [0-24] in original page
│   ├─ Link pages: original.next = new_page_id
│   └─ Promote first key of new page: "user:1025"
│
├─ Propagate to Parent:
│   ├─ Insert promoted key into parent internal node
│   └─ If parent is full, recursively split
│
└─ Root Split (if needed):
    ├─ Create new root page
    ├─ Old root becomes left child
    ├─ New page becomes right child
    └─ Tree height increases by 1
```

---

## 🔍 Search Operation Flow

```
SEARCH("product:2001")
│
├─ 1. Start at Root (Page 1 - Internal Node)
│     ├─ Entries: ["product:2000"→Page 2, "user:1000"→Page 3]
│     ├─ "product:2001" ≥ "product:2000" but < "user:1000"
│     └─ Follow: Page 2
│
├─ 2. Arrive at Leaf (Page 2 - Leaf Node)
│     ├─ Scan entries sequentially
│     ├─ Found: "product:2001" → Data Page 11
│     └─ Return data page ID
│
├─ 3. Fetch Data Page (Page 11)
│     └─ DiskManager reads page at offset (11 * 4096)
│
└─ 4. Extract Value
      └─ Return: "Laptop, $1299.99"

Total Disk I/O: 3 page reads (1 internal + 1 leaf + 1 data)
```

**Key Insight**: For a tree with 1,000,000 records and order 50:
- Tree height ≈ 4 levels
- Max disk reads = 4 (index) + 1 (data) = 5 I/Os
- Compared to linear scan: 1,000,000 I/Os

---

## ⚡ Performance Analysis

### B+ Tree vs. Other Index Structures

| Feature | B+ Tree | Hash Index | Binary Search Tree | Linear Scan |
|---------|---------|------------|-------------------|-------------|
| **Point Query** | O(log n) | O(1) average | O(log n) balanced, O(n) worst | O(n) |
| **Range Query** | O(log n + k) | Not supported | O(log n + k) | O(n) |
| **Disk I/O** | ⭐ Minimal (log height) | Moderate | ⚠️ Poor (many random) | ❌ Terrible |
| **Cache Locality** | ⭐ Excellent | Moderate | ⚠️ Poor | Good |
| **Ordered Scan** | ⭐ Native support | Not supported | In-order traversal | Native |
| **Insert Cost** | O(log n) | O(1) average | O(log n) | O(1) append |
| **Worst Case** | ⭐ Guaranteed | Hash collisions | ❌ Degenerate (linked list) | Always O(n) |

### Why B+ Trees Win for Databases

#### 1. **Minimized Disk I/O**
```
Traditional BST:
  1000 records → ~10 nodes traversed → 10 disk reads
  
B+ Tree (order=50):
  1000 records → ~3 levels → 3 disk reads
  100,000 records → ~4 levels → 4 disk reads
  
Reason: High fanout (50 children per node) reduces tree height
```

#### 2. **Page-Level Granularity**
- One disk read fetches entire 4KB page (multiple keys)
- Node keys fit in single page → better cache utilization
- OS page cache naturally aligns with B+ tree node size

#### 3. **Sequential Access Patterns**
```
Range Query: SELECT * WHERE key BETWEEN 'user:1000' AND 'user:1050'

B+ Tree:
  ├─ Navigate to 'user:1000' (3 I/Os)
  └─ Follow leaf links sequentially (2 I/Os)
  Total: 5 I/Os
  
Hash Index:
  ├─ Must probe each key individually
  └─ 50 random lookups = 50 I/Os
  Total: 50 I/Os
```

#### 4. **Guaranteed Balance**
- All leaves at same depth
- No worst-case degradation
- Predictable performance for capacity planning

---

## 🛠️ Building and Running

### Compilation

```bash
g++ -std=c++17 -O2 -Wall -Wextra database_engine.cpp -o db_engine
```

### Execution

```bash
./db_engine
```

### Expected Output

```
=== Custom Database Engine Demo ===

Inserting records...
  ✓ Inserted: user:1001
  ✓ Inserted: user:1002
  ✓ Inserted: user:1003
  ...

=== Search Operations ===
  Found [user:1002]: Bob Smith, Database Administrator
  Found [product:2001]: Laptop, $1299.99
  Not found: user:9999

=== Database Statistics ===
Total pages: 13
Root page ID: 0
```

---

## 🔬 Code Walkthrough

### Key Design Decisions

#### 1. **Fixed-Size Pages (4096 bytes)**
- Aligns with OS page size for optimal I/O
- Simplifies disk management (no fragmentation handling)
- Enables memory-mapped file optimizations (future work)

#### 2. **Packed Structures**
```cpp
struct PageHeader {
    PageType type;          // Identifies page purpose
    PageID page_id;         // Self-reference for validation
    uint32_t record_count;  // Number of active entries
    PageID next_page_id;    // For leaf node linking
} __attribute__((packed));
```
- `__attribute__((packed))` prevents compiler padding
- Ensures exact binary layout for disk persistence

#### 3. **Separation of Concerns**
- **DiskManager**: Pure I/O, no logic about content
- **BPlusTree**: Index logic, delegates storage to DiskManager
- **StorageEngine**: High-level API, hides complexity

#### 4. **In-Memory vs. On-Disk**
```
In-Memory:
  - Tree traversal algorithm
  - Key comparison logic
  - Split decision logic
  
On-Disk:
  - Node contents (keys + pointers)
  - Persistent after restart
  - Accessed via page IDs
```




