# File System Simulator

A production-quality hierarchical file system simulator demonstrating advanced Object-Oriented Programming (OOP) design patterns and efficient data structures in C++11.

## Overview

This project showcases four major OOP design patterns working together in a real-world scenario:

- **Polymorphism**: Type-safe File and Directory node hierarchy
- **Factory Pattern**: Centralized node creation and management
- **Visitor Pattern**: Flexible tree traversals without modifying core classes
- **Composite Pattern**: Natural hierarchical file system representation

## Features

- ✅ Create, delete, copy, and move files and directories
- ✅ Hierarchical path navigation and resolution
- ✅ Tree-wide search and metadata queries
- ✅ Recursive deep cloning with metadata preservation
- ✅ O(d·log n) path navigation complexity
- ✅ Production-ready with zero memory leaks

## Quick Start

### Run the Project

```bash
cd file_system_simulator
./file_system.exe
```

### Build from Source

```bash
cd file_system_simulator
g++ -std=c++11 -Wall -Wextra -O2 file_system.cpp -o file_system
./file_system
```

### Using Make

```bash
cd file_system_simulator
make run
```

## Design Patterns Used

| Pattern          | Purpose                  | Implementation                           |
| ---------------- | ------------------------ | ---------------------------------------- |
| **Polymorphism** | Different node behaviors | File/Directory inherit from Node         |
| **Factory**      | Centralized creation     | NodeFactory::createFile/createDirectory  |
| **Visitor**      | Flexible traversals      | PrintVisitor, SearchVisitor, SizeVisitor |
| **Composite**    | Tree hierarchy           | Directory contains Nodes                 |

## Algorithm Complexity

| Operation             | Complexity | Notes                        |
| --------------------- | ---------- | ---------------------------- |
| Create file/directory | O(d log n) | d = path depth, n = children |
| Delete node           | O(d log n) | Navigation + removal         |
| Copy subtree          | O(n)       | Deep recursive clone         |
| Search                | O(n)       | Full tree traversal          |
| Get size              | O(n)       | Recursive aggregation        |

## Documentation

For detailed information, see the documentation in the `others/` folder:

- **others/PROJECT_SUMMARY.md** - Architecture analysis and design decisions
- **others/INDEX.md** - Complete navigation guide and file index

## Code Quality

- ✅ Compiles with no warnings (`-Wall -Wextra`)
- ✅ C++11 standard compatible
- ✅ Zero memory leaks (verified)
- ✅ Const-correct design
- ✅ Professional error handling
- ✅ ~40% code comments

## Building Requirements

- C++11 compatible compiler (g++, clang, MSVC)
- Make (optional, for build automation)
- Standard C++ library

## Example Usage

```cpp
FileSystem fs;

// Create directories
fs.createDirectory("/home/user/documents");

// Create files
fs.createFile("/home/user/documents/resume.txt", 2048);

// Display tree
fs.printTree();

// Search
vector<string> results = fs.searchNode("documents");

// Get total size
size_t totalSize = fs.getTotalSize();

// Copy file
fs.copyNode("/home/user/documents/resume.txt", "/home/user/downloads/");

// Delete file
fs.deleteNode("/home/user/downloads/resume.txt");
```

## Technical Specifications

| Aspect           | Details                                       |
| ---------------- | --------------------------------------------- |
| Language         | C++11                                         |
| Lines of Code    | ~430                                          |
| Classes          | 9 (2 concrete, 3 visitors, 4 utilities)       |
| Design Patterns  | 4 (Polymorphism, Factory, Visitor, Composite) |
| Time Complexity  | O(d log n) for operations                     |
| Space Complexity | O(n) where n = total nodes                    |
| Memory Overhead  | Minimal (tree structure only)                 |
| Build Time       | <1 second                                     |

## Learning Outcomes

By studying this project, you'll understand:

- ✅ Polymorphism and virtual methods in C++
- ✅ Factory pattern benefits and implementation
- ✅ Visitor pattern for separation of concerns
- ✅ Composite pattern for hierarchies
- ✅ Tree data structures and traversal
- ✅ Algorithm complexity analysis
- ✅ Professional code organization
- ✅ Memory-safe C++ practices

## License

This is a portfolio/educational project.

## Author

Deviprasad V K
