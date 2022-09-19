#include "heap.h"

#include "debug.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

//A struct to hold the backtrace information of each address of memory alloc'd using heap_alloc()
typedef struct backtrace_t
{
	void* address;
	void* trace[4];
	unsigned short frames;
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
		backtrace_t* backtrace = (backtrace_t*)((char*)address + size);
		backtrace->address = address;
		backtrace->frames = debug_backtrace(backtrace->trace, 4);
		backtrace->size = size /* + sizeof(backtrace_t)*/;
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

	HANDLE process = GetCurrentProcess();
	PIMAGEHLP_SYMBOL64 symbol;

	SymInitialize(process, NULL, TRUE);

	symbol = (IMAGEHLP_SYMBOL64*)calloc(sizeof(IMAGEHLP_SYMBOL64) + 256 * sizeof(char), 1);
	symbol->MaxNameLength = 255;
	symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);

	backtrace_t* trace = heap->backtrace;
	while (trace)
	{
		debug_print(k_print_warning, "Memory leak of size %d bytes with callstack:\n", (int)trace->size);
		for (unsigned int i = 0; i < trace->frames; i++)
		{
			SymGetSymFromAddr64(process, (DWORD64)(trace->trace[i]), 0, symbol);
			debug_print(k_print_warning, "[%i] %s\n", trace->frames - i - 1, symbol->Name);
		}
		
		trace = trace->next;
	}

	free(symbol);
	SymCleanup(process);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}
	
	VirtualFree(heap, 0, MEM_RELEASE);
}
