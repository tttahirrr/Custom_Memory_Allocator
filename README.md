README.md

# Custom Memory Allocator with Implicit Free List
# Overview
This project implements a dynamic memory allocator in C, mimicking malloc and free, using an implicit free list with immediate coalescing. It handles block allocation, splitting, and merging (coalescing) of memory blocks for efficient memory usage.

# Key Features
Block-based heap management with headers encoding block size and allocation status.
Double-word alignment (8 bytes).
Placement policies: Uses first-fit search for free blocks.
Immediate coalescing of adjacent free blocks to reduce fragmentation.
Support functions: mm_init, mm_malloc, mm_free, and coalesce.

# Implementation Highlights
Block Format: Each block includes a header, payload, and optional footer.
Header encodes size and allocated/free bit.
Minimum block size: 16 bytes (header + footer + alignment).
Heap Layout:
Starts with a prologue block.
Ends with an epilogue block to simplify coalescing.
# Macros used for manipulation:
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
# Usage Instructions
Build
Compile with gcc:
gcc -o allocator mm.c memlib.c -Wall -Werror
# Run
Use the allocator within the test program that rely on custom malloc/free behavior.