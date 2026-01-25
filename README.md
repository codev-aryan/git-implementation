# Git Implementation in C++23

<div align="center">

![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.13+-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![Systems](https://img.shields.io/badge/Systems-Programming-green?style=for-the-badge)
![Protocol](https://img.shields.io/badge/Binary-Protocol-orange?style=for-the-badge)

### A From-Scratch Git Implementation Featuring Manual Binary Protocol Parsing and Delta Compression

**Demonstrates deep expertise in systems programming, binary protocols, and distributed version control**

[Quick Start](#-quick-start) • [Technical Deep Dives](#-technical-deep-dives) • [Architecture](#-architecture) • [Build Instructions](#-building--running)

</div>

---

## 🎯 Project Overview

This is a **fully-functional Git client** written in modern C++23, implementing Git's core functionality entirely from scratch without using libgit2 or similar libraries. The project reconstructs Git's internal mechanisms including the packfile protocol, delta compression algorithms, and content-addressable storage system.

### Why This Project Matters

As a **portfolio project for software engineering roles**, this demonstrates:

- ✅ **Binary protocol implementation** with manual parsing of variable-length integers and packed headers
- ✅ **Delta compression resolution** reconstructing files from OFS_DELTA and REF_DELTA instructions
- ✅ **Systems programming expertise** with direct filesystem interaction and raw binary stream handling
- ✅ **Software architecture** with modular design separating networking, storage, and utilities
- ✅ **Production-grade code** with clean abstractions and modern C++23 practices

---

## 🚀 Core Capabilities

### Repository Management
- **`init`**: Initialize a `.git` directory with proper object/refs/HEAD structure
- **`cat-file -p`**: Read and decompress Git objects (blobs, trees, commits) directly from disk
- **`hash-object -w`**: Hash file contents using SHA-1 and store as blob objects
- **`ls-tree`**: Parse tree objects to list directory contents with modes and hashes
- **`write-tree`**: Recursively convert working directory into Git tree objects
- **`commit-tree`**: Create commit objects linking to trees, parents, and metadata

### Network Operations
- **`clone`**: Full repository cloning from remote URLs
  - HTTP smart protocol handshake with `git-upload-pack`
  - Packfile reception and binary parsing
  - Delta resolution to reconstruct complete objects
  - Working directory checkout from resolved trees

---

## 🔬 Technical Deep Dives

### 1. Manual Binary Packfile Parsing

Git's packfile format uses a compact binary representation where object metadata is encoded using variable-length integers with MSB continuation bits. Implemented complete parsing pipeline:

**Variable-Length Integer Decoding:**
```cpp
// Each byte has 7 bits of data + 1 continuation bit (MSB)
while (byte & 0x80) {  // MSB continuation bit set
    value |= (byte & 0x7F) << shift;
    shift += 7;
    byte = read_next_byte();
}
value |= (byte & 0x7F) << shift;
```

**Object Type Extraction:**
- 3-bit type field embedded in packed headers (bits 4-6 of first byte)
- Differentiate between: `OBJ_COMMIT` (1), `OBJ_TREE` (2), `OBJ_BLOB` (3), `OBJ_OFS_DELTA` (6), `OBJ_REF_DELTA` (7)
- Handle size encoding where first byte contains both type and partial size

**Challenges Solved:**
- Byte-level stream management without buffering entire packfile (memory efficiency for 100MB+ packs)
- Handling variable-length fields that span arbitrary byte boundaries
- Maintaining precise offset tracking for delta base references
- Parsing compressed zlib streams embedded within the packfile

### 2. Delta Compression Resolution Engine

Git minimizes network transfer by sending deltas (differences) instead of full objects. Implemented both delta types from specification:

**OFS_DELTA (Offset-Based):**
- Base object referenced by **negative offset** within packfile
- Requires maintaining offset-to-object mapping during streaming parse
- Example: Object at byte 5000 references base at offset -2000 → base is at byte 3000
- More common in modern Git (smaller encoding than REF_DELTA)

**REF_DELTA (Hash-Based):**
- Base object identified by 20-byte SHA-1 hash
- Requires hash table lookup across already-parsed objects
- Must handle forward references where base hasn't been parsed yet (requires multi-pass or buffering)

**Instruction Decoding:**
Implemented the copy/insert opcode system:
```cpp
// Copy instruction (opcode & 0x80): Copy N bytes from base at offset X
if (opcode & 0x80) {
    size_t offset = decode_variable_offset(opcode);  // Bits 0-3
    size_t size = decode_variable_size(opcode);      // Bits 4-6
    result.append(base_object.data() + offset, size);
}
// Insert instruction: Insert N literal bytes from delta stream
else {
    size_t count = opcode;  // Lower 7 bits = byte count
    result.append(read_bytes(count));
}
```

**Performance Considerations:**
- Streamed reconstruction to avoid loading entire packfiles into memory
- Efficient buffer management for frequently-accessed base objects (LRU cache potential)
- Recursive delta chain resolution (delta → delta → delta → base)
- Validation: reconstructed object must match expected size in delta header

### 3. HTTP Smart Protocol Implementation

Implemented Git's HTTP transport layer without external networking libraries:

**Upload-Pack Handshake:**
1. **Discovery**: `GET /info/refs?service=git-upload-pack`
   - Parse server capabilities (multi_ack, side-band-64k, etc.)
   - Extract available refs (refs/heads/*, refs/tags/*)
   
2. **Negotiation**: `POST /git-upload-pack`
   - Send `want <commit-hash>` for requested commits
   - Send `done` (no `have` in initial clone)
   
3. **Packfile Reception**:
   - Parse multiplexed sideband format (channel 1: data, 2: progress, 3: errors)
   - Handle chunked transfer encoding
   - Verify packfile checksum (trailing 20-byte SHA-1)

**Protocol State Machine:**
- Parse pkt-line format (4-byte hex length prefix + payload)
- Handle flush-pkt (0000) delimiters
- Graceful error handling for network failures and malformed responses

### 4. Content-Addressable Storage System

Implemented Git's object storage model with proper hash-based addressing:

**SHA-1 Hashing Pipeline:**
```
raw_content → prepend "blob <size>\0" → SHA-1 hash → hex encoding
Example: "hello world" → "blob 11\0hello world" → hash → "95d09f2b..."
Storage path: .git/objects/95/d09f2b...
```

**Zlib Compression:**
- Deflate algorithm for object compression (typical 60-70% size reduction)
- Inflate for decompression during reads
- Custom wrappers around zlib for Git's specific loose object format

**Directory Structure:**
```
.git/
├── objects/
│   ├── 95/
│   │   └── d09f2b...  # First 2 hex chars → subdir (avoids filesystem limits)
│   └── pack/          # Packfiles from clones
├── refs/
│   └── heads/
│       └── master     # Branch pointers
└── HEAD               # Current branch reference
```

---

## 🏗️ Architecture

### Modular Design Philosophy

The codebase is organized into three focused modules with clear separation of concerns:

```
src/
├── Cloner.cpp          # Network layer (HTTP, packfile, delta resolution)
├── Cloner.hpp
├── Repository.cpp      # Storage layer (object model, .git management)
├── Repository.hpp
├── Utils.cpp           # Primitives (SHA-1, hex conversion, zlib)
├── Utils.hpp
└── main.cpp            # Command dispatcher
```

**Separation of Concerns:**

| Module | Responsibility | Key Functions |
|--------|---------------|---------------|
| **Cloner** | Network operations, binary parsing | `fetch_packfile()`, `parse_objects()`, `resolve_deltas()` |
| **Repository** | Git object model, filesystem I/O | `read_object()`, `write_object()`, `parse_tree()`, `create_commit()` |
| **Utils** | Low-level primitives | `sha1_hash()`, `hex_encode()`, `zlib_compress()`, `zlib_decompress()` |
| **main** | CLI argument parsing, command routing | `cmd_init()`, `cmd_clone()`, `cmd_cat_file()`, `cmd_hash_object()` |

**Design Rationale:**
- **Cloner isolates complexity**: All networking, HTTP protocol, packfile parsing, and delta logic contained in one module
- **Repository provides clean interface**: High-level API for Git operations without exposing storage details
- **Utils enforces DRY**: Common operations (hashing, compression) implemented once, reused everywhere
- **main remains minimal**: Pure command dispatcher with zero business logic

### Data Flow Example: `git clone <url> <dir>`

```
1. main.cpp
   └─> Parse URL and target directory arguments

2. Cloner.cpp
   ├─> HTTP GET request to /info/refs?service=git-upload-pack
   ├─> Parse server capabilities and refs
   ├─> HTTP POST to /git-upload-pack with want/done
   ├─> Receive binary packfile stream (potentially 100MB+)
   ├─> Parse packfile header (signature, version, object count)
   ├─> Stream parse objects:
   │   ├─> OBJ_COMMIT → decompress and store
   │   ├─> OBJ_TREE → decompress and store
   │   ├─> OBJ_BLOB → decompress and store
   │   ├─> OBJ_OFS_DELTA → resolve using offset lookup
   │   └─> OBJ_REF_DELTA → resolve using SHA-1 lookup
   └─> Verify packfile checksum

3. Repository.cpp
   ├─> Store all resolved objects in .git/objects/
   ├─> Parse HEAD commit to find root tree
   ├─> Recursively checkout tree:
   │   ├─> Read tree object
   │   ├─> For each entry:
   │   │   ├─> If blob → write file to working directory
   │   │   └─> If tree → recurse into subdirectory
   ├─> Update .git/HEAD to point to master
   └─> Write .git/refs/heads/master with commit hash

4. Utils.cpp (used throughout)
   ├─> SHA-1 hashing for object addressing
   ├─> Zlib decompression for reading objects
   ├─> Hex encoding for human-readable hashes
   └─> Binary parsing helpers (read_varint, read_offset_encoding)
```

---

## 📂 Project Structure

```
.
├── CMakeLists.txt             # Build configuration
├── README.md                  # This file
├── src
│   ├── Cloner.cpp             # HTTP client, packfile parser, delta resolver
│   ├── Cloner.hpp
│   ├── Repository.cpp         # Object storage, tree parsing, commit creation
│   ├── Repository.hpp
│   ├── Utils.cpp              # SHA-1, hex, zlib wrappers
│   ├── Utils.hpp
│   └── main.cpp               # Entry point, command dispatcher
├── vcpkg-configuration.json   # vcpkg baseline and registry config
└── vcpkg.json                 # Dependency manifest
```

**Design Principles:**
- **Modularity**: Each component has a single, well-defined responsibility
- **Testability**: Functions are pure where possible, side effects isolated
- **Maintainability**: Clear interfaces between modules minimize coupling

---

## 🛠 Building & Running

### Prerequisites

```bash
# Required
- C++23 compiler (GCC 13+, Clang 16+, MSVC 2022+)
- CMake 3.13+
- OpenSSL (for SHA-1 cryptographic hashing)
- Zlib (for compression/decompression)
- vcpkg (optional, for automated dependency management)
```

### Quick Start

```bash
# 1. Clone repository
git clone https://github.com/codev-aryan/git-implementation.git
cd git-implementation

# 2. Build with vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Or build without vcpkg (requires system-installed OpenSSL and Zlib)
cmake -B build -S .
cmake --build build

# 3. The executable is located at build/git
```

### Usage Examples

**Initialize a Repository:**
```bash
./build/git init
# Initialized empty Git repository in .git/
```

**Inspect Git Objects:**
```bash
# Read and decompress a blob/tree/commit
./build/git cat-file -p 95d09f2b10159347eece71399a7e2e907ea3df4f

# List contents of a tree object
./build/git ls-tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904
100644 blob 95d09f2b... README.md
040000 tree a1b2c3d4... src
```

**Create Git Objects:**
```bash
# Hash and store a file as a blob
echo "hello world" > test.txt
./build/git hash-object -w test.txt
# 95d09f2b10159347eece71399a7e2e907ea3df4f

# Write current directory state as a tree
./build/git write-tree
# 4b825dc642cb6eb9a060e54bf8d69288fbee4904

# Create a commit object
./build/git commit-tree 4b825dc6 -p a1b2c3d4 -m "Initial commit"
# e83c5163316f89bfbde7d9ab23ca2e25604af290
```

**Clone a Remote Repository:**
```bash
# Full clone with packfile delta resolution
./build/git clone https://github.com/user/sample-repo.git my-repo
# Cloning into 'my-repo'...
# Receiving objects: 100% (15/15), done.
# Resolving deltas: 100% (3/3), done.

cd my-repo
ls -la
# .git/
# README.md
# src/
```

---

## 🎓 Key Learning Outcomes

This project demonstrates mastery of:

### Systems Programming
- Direct filesystem manipulation with POSIX APIs (`open()`, `read()`, `write()`, `mkdir()`)
- Binary stream processing with precise byte-level control
- Memory management for large data structures (packfiles can be 100MB+)
- Cross-platform compatibility considerations

### Binary Protocol Implementation
- Variable-length integer encoding/decoding (MSB continuation bits)
- Bit manipulation for extracting packed fields (type, size)
- Endianness handling for network protocols
- Streaming parsers that process data incrementally

### Distributed Version Control
- **Content-Addressable Storage**: How SHA-1 hashes create an immutable, distributed data structure
- **Directed Acyclic Graphs (DAGs)**: Modeling commit history where each node references parents
- **Delta Compression**: How Git reduces repository size by 80-90% compared to full snapshots
- **Network Protocols**: HTTP smart protocol for efficient client-server communication

### Software Architecture
- Separation of concerns through modular design
- Clean interfaces between layers (network, storage, utilities)
- Single Responsibility Principle applied to each module
- Testability through isolated, pure functions where possible

---

## 🏆 Implementation Milestones

Development followed a progressive complexity model:

| Milestone | Feature | Technical Challenge |
|-----------|---------|-------------------|
| **Phase 1** | `init`, `cat-file`, `hash-object` | Filesystem I/O, SHA-1 hashing, zlib compression |
| **Phase 2** | `ls-tree`, `write-tree` | Tree object parsing, recursive directory traversal |
| **Phase 3** | `commit-tree` | Commit object creation, parent references |
| **Phase 4** | **HTTP protocol** | Socket programming, HTTP request/response handling |
| **Phase 5** | **Packfile parsing** | Binary format decoding, variable-length integers |
| **Phase 6** | **Delta resolution** | OFS_DELTA/REF_DELTA reconstruction, instruction parsing |
| **Phase 7** | **Full clone** | End-to-end integration, working directory checkout |

Each phase required deep understanding of Git internals and careful attention to specification details.

---

## 🔮 Future Enhancements

Potential extensions demonstrating additional expertise:

- **Push Support**: Implement `git push` with pack generation and ref updates
- **Index Management**: Staging area with `.git/index` file format
- **Branch Operations**: `git branch`, `git checkout`, `git merge`
- **Diff Engine**: Text diffing algorithms (Myers, Patience)
- **Pack Generation**: Creating packfiles for efficient storage/transfer
- **Git LFS Support**: Large file handling with pointer files
- **Shallow Clones**: `--depth` parameter for partial history
- **Sparse Checkout**: Selective working directory population

---

## 📞 Contact & Links

**Developer**: Aryan Mehta  
**Repository**: [github.com/codev-aryan/git-implementation](https://github.com/codev-aryan/git-implementation)  
**LinkedIn**: [Connect with me](https://linkedin.com/in/codev-aryan)  

---

<div align="center">

**Built with modern C++23 to demonstrate proficiency in systems programming and protocol implementation**

⭐ Star this repo if you find it impressive! ⭐

[Report Bug](https://github.com/codev-aryan/git-implementation/issues) • [Request Feature](https://github.com/codev-aryan/git-implementation/issues)

</div>