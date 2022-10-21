#include "trace.h"
#include "heap.h"
#include "fs.h"
#include "queue.h"
#include "timer.h"
#include "debug.h"
#include "mutex.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>

typedef struct event_t
{
	char* name;
	int event_type; //0 = begin, 1 = end
	int pid;
	int tid;
	int ts;

} event_t;

typedef struct event_stack_t
{
	char* name;
	struct event_stack_t* next;
} event_stack_t;

typedef struct thread_list_t
{
	int tid;
	struct thread_list_t* next;
	struct event_stack_t* event_next;
} thread_list_t;

typedef struct trace_t
{
	heap_t* heap;
	fs_t* fs;
	mutex_t* mutex;
	thread_list_t* thread_list;
	event_t** event_t_array;
	size_t event_count;
	char* file_buffer;
	size_t file_size;
	char* file_path;
	short tracing;

} trace_t;

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->fs = fs_create(heap, 1);
	trace->mutex = mutex_create();

	trace->thread_list = heap_alloc(heap, sizeof(thread_list_t), 8);
	trace->thread_list->tid = GetCurrentThreadId();
	//trace->thread_list->event_next = heap_alloc(heap, sizeof(event_stack_t), 8);

	trace->event_t_array = heap_alloc(heap, sizeof(event_t*) * event_capacity, 8);
	trace->event_count = 0;
	trace->file_buffer;
	trace->tracing = 0;
	return trace;
}

void trace_destroy(trace_t* trace)
{
	mutex_lock(trace->mutex);
	thread_list_t* temp = trace->thread_list;
	while (temp)                 
	{
		event_stack_t* event_temp = temp->event_next;
		while (event_temp)
		{
			event_stack_t* next_event = event_temp->next;
			heap_free(trace->heap, event_temp);
			event_temp = next_event;
		}
		thread_list_t* next_thread = temp->next;
		heap_free(trace->heap, temp);
		temp = next_thread;
	}

	for (int i = 0; i <= trace->event_count; i++)
	{
		heap_free(trace->heap, trace->event_t_array[i]);
	}

	heap_free(trace->heap, trace->event_t_array);
	fs_destroy(trace->fs);
	mutex_unlock(trace->mutex);
	mutex_destroy(trace->mutex);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{

	if (trace->tracing == 1)
	{
		mutex_lock(trace->mutex);
		//create an event_t and store all of the current information
		event_t* temp = heap_alloc(trace->heap, sizeof(event_t), 8);
		temp->name = (char*)name;
		temp->event_type = 0;
		temp->pid = GetCurrentProcessId();
		temp->tid = GetCurrentThreadId();
		temp->ts = (int)timer_ticks_to_us(timer_get_ticks());


		thread_list_t* thread_list_temp = trace->thread_list;
		bool added = false;
		//search for a thread matching the current tid
		while (thread_list_temp)
		{
			if (thread_list_temp->tid == temp->tid)
			{
				//create a new event stack and allocate memory for it and its name
				event_stack_t* new_stack = heap_alloc(trace->heap, sizeof(event_stack_t), 8);

				new_stack->name = (char*)name; 

				new_stack->next = thread_list_temp->event_next;
				thread_list_temp->event_next = new_stack;

				added = true;
				break;
			}
			thread_list_temp = thread_list_temp->next;
		}
		//if we did not find a thread list for the current thread, make one
		if (!added)
		{
			//new thread list node with tid == to current tid
			thread_list_t* new_list = heap_alloc(trace->heap, sizeof(thread_list_t), 8);
			new_list->tid = temp->tid;
			//alloc a new event stack for that thread
			new_list->event_next = heap_alloc(trace->heap, sizeof(event_stack_t), 8);

			new_list->event_next->name = (char*)name;

			//put the new thread list node at the head of the thread list
			new_list->next = trace->thread_list;
			trace->thread_list = new_list;
		}
		
		//add the event to the list of pushes and pops
		trace->event_t_array[trace->event_count] = temp;
		trace->event_count += 1;
		mutex_unlock(trace->mutex);
	}
}

void trace_duration_pop(trace_t* trace)
{
	if (trace->tracing == 1)
	{
		mutex_lock(trace->mutex);
		thread_list_t* thread_list_temp = trace->thread_list;
		event_t* temp = heap_alloc(trace->heap, sizeof(event_t), 8);
		
		int thread_id = GetCurrentThreadId();
		while (thread_list_temp)
		{
			if (thread_list_temp->tid == thread_id)
			{
				
				temp->name = thread_list_temp->event_next->name;

				event_stack_t* extra = thread_list_temp->event_next->next;
				heap_free(trace->heap, thread_list_temp->event_next);
				thread_list_temp->event_next = extra;

				break;
			}
			thread_list_temp = thread_list_temp->next;
		}

		temp->event_type = 1;
		temp->pid = GetCurrentProcessId();
		temp->tid = GetCurrentThreadId();
		temp->ts = (int)timer_ticks_to_us(timer_get_ticks());

		trace->event_t_array[trace->event_count] = temp;
		trace->event_count += 1;
		mutex_unlock(trace->mutex);
	}
}

void trace_capture_start(trace_t* trace, const char* path)
{
	trace->file_path = (char*)path;
	trace->file_size = 0;
	trace->tracing = 1;
}

void trace_capture_stop(trace_t* trace)
{
	mutex_lock(trace->mutex);
	int size = snprintf(NULL, 0, "{\n\t \"displayTimeUnit\": \"ns\", \"traceEvents\" : [\n");
	event_t* temp;
	for (int i = 0; i < trace->event_count; i++)
	{
		temp = trace->event_t_array[i];
		size += snprintf(NULL, 0, "{\t\t\"name\":\"%s\",\"ph\":\"B\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%d\"},\n", temp->name, temp->pid, temp->tid, temp->ts);
	}
	size += snprintf(NULL, 0, "\t]\n}");
	debug_print(k_print_info, "%d\n", size);
	char* file_buffer = heap_alloc(trace->heap, sizeof(char) * size, 8);
	debug_print(k_print_error, "%p\n", file_buffer);
	
	trace->file_size += snprintf(file_buffer, size, "{\n\t \"displayTimeUnit\": \"ns\", \"traceEvents\" : [\n");
	debug_print(k_print_error, "%p\n", file_buffer);
	for (int i = 0; i < trace->event_count; i++)
	{
		temp = trace->event_t_array[i];
		if (temp->event_type == 0)
		{
			
			trace->file_size += snprintf((file_buffer + trace->file_size), size, "\t\t{\"name\":\"%s\",\"ph\":\"B\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%d\"},\n", temp->name, temp->pid, temp->tid, temp->ts);
			debug_print(k_print_error, "%p\n", file_buffer);
		}
		else 
		{
			trace->file_size += snprintf((file_buffer + trace->file_size), size, "\t\t{\"name\":\"%s\",\"ph\":\"E\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%d\"},\n", temp->name, temp->pid, temp->tid, temp->ts);
			debug_print(k_print_error, "%p\n", file_buffer);
		}
	}
	trace->file_size += snprintf( (file_buffer + trace->file_size), size, "\t]\n}");
	debug_print(k_print_info,"pre: %p\n", file_buffer);
	fs_work_t* w = fs_write(trace->fs, trace->file_path, file_buffer, trace->file_size, false);
	debug_print(k_print_info, "post: %p\n", file_buffer);
	heap_free(trace->heap, file_buffer);
	fs_work_wait(w);
	fs_work_destroy(w);
	
	debug_print(k_print_info, "wrote\n");
	trace->tracing = 0;
	mutex_unlock(trace->mutex);
}