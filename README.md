# Custom Database Engine

A simple, educational B+ Tree-based custom database engine written in C++17. 
This project demonstrates how a relational-style database physically stores data on disk using a 4KB fixed-size paging system, a custom `DiskManager`, and an internal `BPlusTree` index for fast lookups.

## Features
- **Disk Manager**: Handles reading and writing raw 4KB blocks (`Pages`) directly to disk (`test.db`).
- **B+ Tree Index**: Automatically structures records in an N-ary tree, allowing extremely fast searching without scanning the entire file. Splits and promotes keys automatically as it grows.
- **Storage Engine Wrapper**: A simple API combining the disk manager and the B+ tree for seamless inserts and searches.
- **Zero Dependencies**: Uses only standard C++ libraries (`<iostream>`, `<fstream>`, `<cstring>`, etc.).

## Project Structure
The codebase has been refactored into modular, heavily-commented files:
- `Config.h` - Global constants and configuration limits.
- `Page.h` - Structure definitions for how 4KB memory blocks are arranged.
- `DiskManager.h / .cpp` - Handles raw disk I/O.
- `BPlusTree.h / .cpp` - The indexing logic algorithm.
- `StorageEngine.h / .cpp` - The top-level application wrapper.
- `main.cpp` - A demonstration showing initialization, inserting, and searching.
- `Makefile` - Build instructions.

## Prerequisites
- A C++17 compatible compiler (e.g., `g++` or `clang++`).
- `make` utility.

## How to Build
The project comes with a `Makefile` that compiles the object files cleanly into an `obj/` directory.

To build the project, simply run:
```bash
make
```

To clean up the compiled object files and the executable:
```bash
make clean
```

## How to Run
After compiling, run the generated executable:
```bash
./Engine
```

### Expected Output
The program will start, create a local `test.db` file (if it doesn't already exist), insert 10 sample keys, and test the B+ Tree search logic for specific keys.

```text
=== Custom Database Engine Demo ===

Inserting sample records into the database...
  [SUCCESS] Inserted Key: user:1001
  [SUCCESS] Inserted Key: user:1002
...

=== Running Search Operations ===
  Found [user:1002] -> Bob Smith, Database Administrator
...
```

You can examine the generated `test.db` file in your directory to see the raw binary format of the database.
