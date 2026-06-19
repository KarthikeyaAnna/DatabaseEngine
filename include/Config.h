#pragma once

#include <cstdint>
#include <cstddef>

/**
 * ============================================================================
 * DATABASE CONFIGURATION & CONSTANTS
 * ============================================================================
 * This file defines the core parameters that shape our database engine.
 * Adjusting these values will change how data is stored and indexed on disk.
 */

// We use a fixed 4KB page size, which is standard for many databases 
// as it often matches the operating system's filesystem block size.
constexpr size_t PAGE_SIZE = 4096;

// Maximum length of a string key we can store (in bytes).
constexpr size_t MAX_KEY_SIZE = 64;

// Maximum length of a string value we can store (in bytes).
constexpr size_t MAX_VALUE_SIZE = 256;

// The "order" of our B+ Tree defines the maximum number of keys a single node can hold.
// When a node exceeds this number, it splits into two nodes.
constexpr size_t BTREE_ORDER = 50;

// We use 32-bit unsigned integers to represent Page IDs, allowing up to ~4 billion pages.
using PageID = uint32_t;

// A special constant to indicate a null or invalid page reference (similar to a null pointer).
constexpr PageID INVALID_PAGE_ID = 0xFFFFFFFF;
