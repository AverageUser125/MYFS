// Copyright 2022 Alexey Kutepov <reximkut@gmail.com>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef ARENA_H_
#define ARENA_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <new>
#include <type_traits>
#include "config.h"

#define ARENA_NOSTDIO
#ifndef ARENA_NOSTDIO
#include <stdarg.h>
#include <stdio.h>
#endif // ARENA_NOSTDIO

#ifndef ARENA_ASSERT
#include <assert.h>
#define ARENA_ASSERT assert
#endif

#define ARENA_BACKEND_LIBC_MALLOC 0
#define ARENA_BACKEND_LINUX_MMAP 1
#define ARENA_BACKEND_WIN32_VIRTUALALLOC 2
#define ARENA_BACKEND_WASM_HEAPBASE 3

#ifndef ARENA_BACKEND
#ifdef _WIN32
#define ARENA_BACKEND ARENA_BACKEND_WIN32_VIRTUALALLOC
#elif defined(__linux__)
#define ARENA_BACKEND ARENA_BACKEND_LINUX_MMAP
#endif
#endif // ARENA_BACKEND


struct Region {
	Region* next;
	size_t count;
	size_t capacity;
	uintptr_t data[1]; // flexiable member
};

struct Arena {
	Region* begin = nullptr;
	Region* end = nullptr;
};

struct ArenaSnapshot {
	Region* end;  // The current region (end region)
	size_t count; // The number of used slots in the end region
};

void* arena_alloc(Arena* a, size_t size_bytes);
void* arena_realloc(Arena* a, void* oldptr, size_t oldsz, size_t newsz);
char* arena_strdup(Arena* a, const char* cstr);
void* arena_memdup(Arena* a, void* data, size_t size);
#ifndef ARENA_NOSTDIO
char* arena_sprintf(Arena* a, const char* format, ...);
#endif // ARENA_NOSTDIO

void arena_init(Arena* a, size_t reservedCapacity = REGION_DEFAULT_CAPACITY);
Arena arena_init(size_t reservedCapacity = REGION_DEFAULT_CAPACITY);
Region* new_region(size_t capacity);
void arena_free(Arena* a);
void arena_reset(Arena* a);
void free_region(Region* r);

#endif