#include "arena.h"

#if ARENA_BACKEND == ARENA_BACKEND_LIBC_MALLOC
#include <stdlib.h>

// TODO: instead of accepting specific capacity new_region() should accept the size of the object we want to fit into the region
// It should be up to new_region() to decide the actual capacity to allocate
Region* new_region(size_t capacity) {
	size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
	// TODO: it would be nice if we could guarantee that the regions are allocated by ARENA_BACKEND_LIBC_MALLOC are page aligned
	Region* r = (Region*)malloc(size_bytes);
	ARENA_ASSERT(r);
	r->next = nullptr;
	r->count = 0;
	r->capacity = capacity;

	memset(r->data, 0, capacity);
	return r;
}

void free_region(Region* r) {
	free(r);
}

#elif ARENA_BACKEND == ARENA_BACKEND_LINUX_MMAP
#include <unistd.h>
#include <sys/mman.h>

Region* new_region(size_t capacity) {
	size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
	Region* r = (Region*)mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	ARENA_ASSERT(r != MAP_FAILED);
	r->next = nullptr;
	r->count = 0;
	r->capacity = capacity;

	memset(r->data, 0, capacity);
	return r;
}

void free_region(Region* r) {
	size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * r->capacity;
	int ret = munmap(r, size_bytes);
	(void)ret;
	ARENA_ASSERT(ret == 0);
}

#elif ARENA_BACKEND == ARENA_BACKEND_WIN32_VIRTUALALLOC
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define INV_HANDLE(x) (((x) == nullptr) || ((x) == INVALID_HANDLE_VALUE))

Region* new_region(size_t capacity) {
	SIZE_T size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
	Region* r = (Region*)VirtualAllocEx(GetCurrentProcess(),	  /* Allocate in current process address space */
										nullptr,				  /* Unknown position */
										size_bytes,				  /* Bytes to allocate */
										MEM_COMMIT | MEM_RESERVE, /* Reserve and commit allocated page */
										PAGE_READWRITE			  /* Permissions ( Read/Write )*/
	);
	if (INV_HANDLE(r)) {
		ARENA_ASSERT(0 && "VirtualAllocEx() failed.");
	}
	r->next = nullptr;
	r->count = 0;
	r->capacity = capacity;
	memset(r->data, 0, capacity);
	return r;
}

void free_region(Region* r) {
	if (INV_HANDLE(r))
		return;

	BOOL free_result = VirtualFreeEx(GetCurrentProcess(), /* Deallocate from current process address space */
									 (LPVOID)r,			  /* Address to deallocate */
									 0,					  /* Bytes to deallocate ( Unknown, deallocate entire page ) */
									 MEM_RELEASE		  /* Release the page ( And implicitly decommit it ) */
	);

	if (FALSE == free_result)
		ARENA_ASSERT(0 && "VirtualFreeEx() failed.");
}

#elif ARENA_BACKEND == ARENA_BACKEND_WASM_HEAPBASE
#error "TODO: WASM __heap_base backend is not implemented yet"
#else
#error "Unknown Arena backend"
#endif

// TODO: add debug statistic collection mode for arena
// Should collect things like:
// - How many times new_region was called
// - How many times existing region was skipped
// - How many times allocation exceeded REGION_DEFAULT_CAPACITY

void* arena_alloc(Arena* a, size_t size_bytes) {
	size_t size = (size_bytes + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
	ARENA_ASSERT(a != nullptr);
	ARENA_ASSERT(a->begin != nullptr);
	ARENA_ASSERT(a->end != nullptr);

	while (a->end->count + size > a->end->capacity && a->end->next != nullptr) {
		a->end = a->end->next;
	}

	if (a->end->count + size > a->end->capacity) {
		ARENA_ASSERT(a->end->next == nullptr);
		size_t capacity = REGION_DEFAULT_CAPACITY;
		if (capacity < size)
			capacity = size;
		a->end->next = new_region(capacity);
		a->end = a->end->next;
	}

	void* result = &a->end->data[a->end->count];
	a->end->count += size;
	return result;
}

void* arena_realloc(Arena* a, void* oldptr, size_t oldsz, size_t newsz) {
	if (newsz <= oldsz)
		return oldptr;
	void* newptr = arena_alloc(a, newsz);
	char* newptr_char = (char*)newptr;
	char* oldptr_char = (char*)oldptr;
	for (size_t i = 0; i < oldsz; ++i) {
		newptr_char[i] = oldptr_char[i];
	}
	return newptr;
}

char* arena_strdup(Arena* a, const char* cstr) {
	size_t n = strlen(cstr);
	char* dup = (char*)arena_alloc(a, n + 1);
	memcpy(dup, cstr, n);
	dup[n] = '\0';
	return dup;
}

void* arena_memdup(Arena* a, void* data, size_t size) {
	return memcpy(arena_alloc(a, size), data, size);
}

#ifndef ARENA_NOSTDIO
char* arena_sprintf(Arena* a, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(nullptr, 0, format, args);
	va_end(args);

	ARENA_ASSERT(n >= 0);
	char* result = (char*)arena_alloc(a, n + 1);
	va_start(args, format);
	vsnprintf(result, n + 1, format, args);
	va_end(args);

	return result;
}
#endif // ARENA_NOSTDIO

void arena_init(Arena* a, size_t reservedCapacity) {
	ARENA_ASSERT(a->end == nullptr && a->begin == nullptr);
	a->end = new_region(reservedCapacity);
	a->begin = a->end;
}

Arena arena_init(size_t reservedCapacity) {
	Arena a;
	a.end = new_region(reservedCapacity);
	a.begin = a.end;
	return a;
}

void arena_reset(Arena* a) {
	for (Region* r = a->begin; r != nullptr; r = r->next) {
		r->count = 0;
	}

	a->end = a->begin;
}

void arena_free(Arena* a) {
	Region* r = a->begin;
	while (r) {
		Region* r0 = r;
		r = r->next;
		free_region(r0);
	}
	a->begin = nullptr;
	a->end = nullptr;
}
