#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

#define FRAME_MAX 3

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

//A struct to hold the backtrace information of each address of memory allocated using heap_alloc()
typedef struct backtrace_t
{
	void* address;
	void* trace[FRAME_MAX];
	unsigned short frames; //the number of frames captured
	struct backtrace_t* next; //a pointer to allow a linked list of backtrace_t
	size_t size; //the size of the memory block
} backtrace_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	backtrace_t* backtrace; //a linked list to store backtraces
	mutex_t* mutex;
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

	heap->mutex = mutex_create();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;
	heap->backtrace = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	mutex_lock(heap->mutex);
	//alloc an additional sizeof(backtrace_t) bytes as overhead for the backtrace data
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

	//if we create a valid address of allocated memory, store the backtrace data behind the address
	if (address)
	{
		backtrace_t* backtrace = (backtrace_t*)((char*)address + size);
		backtrace->address = address;
		backtrace->frames = debug_backtrace(backtrace->trace, FRAME_MAX);
		backtrace->size = size;
		backtrace->next = heap->backtrace;
		heap->backtrace = backtrace;
	}

	mutex_unlock(heap->mutex);

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	mutex_lock(heap->mutex);

	//find the backtrace that matches the address being freed and remove it from the linked list
	backtrace_t* trace = heap->backtrace;
	
	if (trace && trace->address == address)
	{
		heap->backtrace = trace->next;
		tlsf_free(heap->tlsf, address);
	}
	else if(trace)
	{
		while (trace->next)
		{
			if (trace->next->address == address)
			{
				trace->next = trace->next->next;
				tlsf_free(heap->tlsf, address);
				break;
			}
			else
			{
				trace = trace->next;
			}
		}
	}
	mutex_unlock(heap->mutex);
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

	//parse through each backtrace_t struct and print its leak information
	backtrace_t* trace = heap->backtrace;
	while (trace)
	{
		debug_print(k_print_warning, "Memory leak of size %d bytes of data and %d bytes of overhead at address %p with callstack:\n", (int)trace->size, (int)sizeof(backtrace_t), trace->address);
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

	mutex_destroy(heap->mutex);

	VirtualFree(heap, 0, MEM_RELEASE);
}
