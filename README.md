# Custom Database Engine

A simple, educational B+ Tree-based custom database engine written in C++17. 
This project demonstrates how a relational-style database physically stores data on disk using a 4KB fixed-size paging system, a custom `DiskManager`, and an internal `BPlusTree` index for fast lookups.

## Features
- **Multithreaded TCP Server**: Listens on port 9000, handling multiple client connections concurrently using a custom Thread Pool.
- **Concurrency & Synchronization**: Ensures safe simultaneous data access with robust read/write locks (`std::shared_mutex`), allowing multiple concurrent readers or exclusive writers.
- **Buffer Pool Manager**: Caches disk pages in memory to dramatically reduce I/O bottlenecks and speed up subsequent data access.
- **Dynamic Collections**: Automatically manages multiple database files (e.g., `users.db`, `orders.db`) within a single server instance.
- **Disk Manager**: Handles reading and writing raw 4KB blocks (`Pages`) directly to disk.
- **B+ Tree Index**: Automatically structures records in an N-ary tree, allowing extremely fast searching without scanning the entire file. Splits and promotes keys automatically as it grows.
- **Storage Engine Wrapper**: A simple API combining the disk manager and the B+ tree for seamless inserts and searches.
- **Zero Dependencies**: Uses only standard C++ libraries (`<iostream>`, `<fstream>`, `<cstring>`, `<thread>`, `<shared_mutex>`, etc.).

## Project Structure
The codebase has been refactored into modular, heavily-commented files:
- `Config.h` - Global constants and configuration limits.
- `Page.h` - Structure definitions for how 4KB memory blocks are arranged.
- `DiskManager.h / .cpp` - Handles raw disk I/O.
- `BufferPoolManager.h / .cpp` - Caches disk pages in memory to reduce expensive I/O operations.
- `BPlusTree.h / .cpp` - The indexing logic algorithm.
- `StorageEngine.h / .cpp` - The storage application wrapper for individual collections.
- `ThreadPool.h` - A custom thread pool implementation for handling concurrent client connections.
- `main.cpp` - The main TCP server implementation, including the `DatabaseManager` for collection routing.
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
After compiling, run the generated executable to start the server:
```bash
./Engine
```

### Expected Output
The program will initialize a Thread Pool and begin listening for TCP connections on port 9000.

```text
=== Custom Database Engine TCP Server ===
Initializing Database Manager...
Thread Pool running with 4 workers.

============================================
SERVER ONLINE & LISTENING ON PORT 9000
To connect, open a new terminal and type:
  telnet localhost 9000
============================================
```

### Connecting to the Server
Open a new terminal window and connect to the database using `telnet`:
```bash
telnet localhost 9000
```

Once connected, you can interact with the server using the following commands:
- `INSERT <collection> <key> <value>`: Inserts a key-value pair into the specified collection.
- `SEARCH <collection> <key>`: Searches for a key in the specified collection.
- `STATS <collection>`: Prints database statistics to the server console.
- `EXIT`: Closes the connection.

Example session:
```text
db> INSERT users user:1001 Bob Smith
[SUCCESS] Inserted key: user:1001 into 'users'
db> SEARCH users user:1001
[FOUND in users] user:1001 -> Bob Smith
db> EXIT
Closing connection. Goodbye!
```
