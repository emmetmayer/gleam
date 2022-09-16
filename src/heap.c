#include "heap.h"

#include "debug.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct backtrace_t
{
	void* address;
	void** trace;
	struct backtrace_t* next;
	size_t size;
} backtrace_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	backtrace_t* backtrace;
} heap_t;

heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;
	heap->backtrace = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	void* address = tlsf_memalign(heap->tlsf, alignment, size + sizeof(backtrace_t));
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) +
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, size + sizeof(backtrace_t));
	}

	if (address)
	{
		backtrace_t* backtrace = (backtrace_t*)((int*)address + size / sizeof(int));
		backtrace->address = address;
		backtrace->trace = (int**)((int*)backtrace + sizeof(int*) / sizeof(int));
		debug_backtrace(backtrace->trace, 10);
		backtrace->next = heap->backtrace;
		heap->backtrace = backtrace;
	}


	return address;
}

void heap_free(heap_t* heap, void* address)
{
	tlsf_free(heap->tlsf, address);
	backtrace_t* trace = heap->backtrace;
	if (trace->address == address)
	{
		heap->backtrace = trace->next;
	}
	else
	{
		while (trace->next->address != address)
		{
			trace = trace->next;
		}
		trace->next = trace->next->next;
	}


}

void heap_destroy(heap_t* heap)
{
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}
	backtrace_t* trace = heap->backtrace;
	while (trace)
	{
		debug_print(k_print_warning, "leaked");
		trace = trace->next;
	}
	VirtualFree(heap, 0, MEM_RELEASE);
}
