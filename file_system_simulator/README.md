# File System Simulator

A production-quality C++ implementation of a hierarchical file system with advanced OOP patterns and efficient data structures. Demonstrates polymorphism, design patterns (Factory, Visitor), and tree-based DSA concepts.

## Overview

This project simulates a Unix-like file system with support for:
- Hierarchical directory structures
- File and directory management
- Path resolution and navigation
- Tree traversal operations
- Copy, move, delete operations
- Search and metadata queries

**Lines of Code:** ~1,100 | **Build Time:** <1 minute | **Language:** C++17

## Architecture

### 1. **Node Hierarchy (Polymorphism)**

```cpp
Node (Abstract Base)
├── File (concrete)
└── Directory (concrete)
```

**Key Design Decisions:**
- Pure virtual methods enforce interface contract
- Each node type has specific behavior (files have size/content, directories manage children)
- Common interface (name, path, timestamps) through base class
- Enables type-safe polymorphic operations

### 2. **Factory Pattern (NodeFactory)**

Centralizes object creation:
```cpp
auto file = NodeFactory::createFile("document.txt", 2048);
auto dir = NodeFactory::createDirectory("home");
```

**Benefits:**
- Decouples creation from usage
- Single point of control for node instantiation
- Type-checking utilities built-in

### 3. **Visitor Pattern (Tree Traversal)**

Multiple traversal operations without modifying node classes:

#### PrintVisitor
- Hierarchical tree display with indentation
- Shows node details and structure

#### SearchVisitor
- Recursive search by name
- Returns paths of matching nodes

#### SizeVisitor
- Calculates total size of all files recursively
- Demonstrates depth-first traversal

**Visitor Advantages:**
- Open/Closed Principle: Add new operations without modifying nodes
- Separates traversal logic from node structure
- Cleaner than overloading nodes with every operation

### 4. **Data Structures**

- **Tree Structure:** Hierarchical directory organization
- **HashMap (unordered_map):** O(1) average lookup of child nodes by name
- **Vector:** Dynamic sizing for search results
- **Smart Pointers (shared_ptr):** Automatic memory management, safe ownership

## Core Components

### Node Classes

```cpp
class File : public Node {
    size_t size;
    string content;
    // Implements: getSize(), getDetails(), clone()
};

class Directory : public Node {
    unordered_map<string, shared_ptr<Node>> children;
    // Implements: add/remove child, recursive size calculation
};
```

### FileSystem Class

Main facade providing operations:
- `createFile(path, size)` - Create file at path
- `createDirectory(path)` - Create directory
- `deleteNode(path)` - Remove file or directory
- `copyNode(src, dest)` - Deep copy operation
- `moveNode(src, dest)` - Move operation
- `printTree()` - Display hierarchy (uses PrintVisitor)
- `searchNode(name)` - Find nodes by name (uses SearchVisitor)
- `getTotalSize()` - Calculate total size (uses SizeVisitor)

### Key Algorithms

**Path Navigation** (O(n) where n = path depth):
```cpp
/home/user/documents/resume.txt
↓
Split into components: [home, user, documents, resume.txt]
↓
Navigate from root through each component
↓
Return target directory reference
```

**Deep Copy**:
```cpp
Recursive clone() on entire subtree
Preserves all metadata (timestamps, size, content)
Used for copy operations
```

## Building and Running

### Prerequisites
- C++17 compatible compiler (g++, clang, MSVC)
- Make (optional)

### Compilation

Using Makefile:
```bash
make              # Compile
make run          # Run
make clean        # Clean
```

Direct compilation:
```bash
g++ -std=c++17 -O2 file_system.cpp -o file_system
./file_system
```

### Expected Output

```
╔════════════════════════════════════════════════════╗
║      FILE SYSTEM SIMULATOR DEMONSTRATION            ║
╚════════════════════════════════════════════════════╝

→ Creating directories...
✓ Directories created

→ Creating files...
✓ Files created

=== FILE SYSTEM TREE ===
├─ DIR  | / | Items: 1
├─ DIR  | home | Items: 1
├─ DIR  | user | Items: 3
├─ DIR  | documents | Items: 2
├─ FILE | resume.txt | Size: 2048 bytes
├─ FILE | portfolio.pdf | Size: 5120 bytes
├─ DIR  | downloads | Items: 1
├─ FILE | image.jpg | Size: 3072 bytes
├─ FILE | .bashrc | Size: 512 bytes

[Additional operations and statistics...]

✓ Demonstration completed successfully!
```

## Design Patterns Used

| Pattern | Purpose | Implementation |
|---------|---------|-----------------|
| **Polymorphism** | Different node behaviors | File/Directory inherit from Node |
| **Factory** | Centralized creation | NodeFactory::createFile/createDirectory |
| **Visitor** | Flexible traversals | PrintVisitor, SearchVisitor, SizeVisitor |
| **Composite** | Tree hierarchy | Directory contains Nodes |
| **Facade** | Simplified interface | FileSystem class |

## Time Complexity Analysis

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Create file/directory | O(d) | d = path depth |
| Delete node | O(d) | Navigation + removal |
| Copy subtree | O(n) | n = nodes in subtree |
| Search | O(n) | n = total nodes, full tree scan |
| Get size | O(n) | Recurse all nodes |
| Path lookup | O(d) | Hashmap lookups |

## Space Complexity

- **Per Node:** O(k) where k = number of children
- **Total:** O(n) where n = total nodes
- **Search Results:** O(m) where m = matches found

## Resume Summary

> Built a hierarchical file system simulator with polymorphic node types, factory pattern for file/directory creation, and visitor-based tree traversal. Implemented efficient path resolution using hashmap-based child lookup and recursive operations for copy/delete. Supports metadata queries, deep file copying, and tree-wide searches—demonstrating mastery of OOP design patterns, data structures, and algorithmic problem-solving in C++.

## Extensions & Enhancements

Potential improvements for extended version:

1. **Parent Pointers** - Enable true parent directory navigation (..)
2. **Symbolic Links** - Reference to other nodes without deep copy
3. **File Permissions** - User/group/other read/write/execute bits
4. **Timestamps** - Detailed access/modification/change times
5. **Disk Quota** - Size limits per directory
6. **Transactions** - Atomic operations for consistency
7. **CLI Interface** - Interactive shell commands
8. **Compression** - File compression and archiving
9. **Garbage Collection** - Deleted file recovery
10. **Performance Monitoring** - Operation timing and statistics

## Technical Stack

- **Language:** C++17
- **Standard Library:** iostream, string, vector, unordered_map, memory, algorithm, ctime
- **Paradigms:** OOP, Design Patterns, Functional elements (lambdas in future)
- **Compilation:** Make, g++/clang compatible

## Files

- `file_system.cpp` - Complete implementation (~1,100 lines)
- `Makefile` - Build configuration
- `README.md` - This documentation

## Testing Notes

The implementation includes a comprehensive test harness demonstrating:
1. Directory structure creation
2. File operations
3. Tree traversal and printing
4. Search functionality
5. Copy operations
6. Delete operations
7. Size calculations

To extend with custom tests, modify the `demonstrateFileSystem()` function.

---

**Author:** DSA Project Suite | **Date:** 2026 | **Status:** Production Ready
