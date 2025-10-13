#ifndef OVERTURE_H_
#define OVERTURE_H_

#ifndef OVDEF 
#define OVDEF 
#endif  /* OVDEF */ 

#ifndef OVINTERNAL 
#define OVINTERNAL static 
#endif /* OVINTERNAL */

// Indicates that this is an output argument (for when return value is written into a pointer argument). 
#define OV_OUT

#ifndef OV_ASSERT
#include <assert.h>
#define OV_ASSERT(x) assert(x)
#endif  /* OV_ASSERT */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef OV_DEFAULT_ALIGNMENT
#define OV_DEFAULT_ALIGNMENT (2u*sizeof(void*))
#endif


// These are generic enough status codes, they can be use in a broad array of situations.
typedef enum OvStatus {
	OV_STATUS_OK,
	OV_STATUS_EMPTY,
	OV_STATUS_OUT_OF_BOUNDS,
	OV_STATUS_ALREADY_EXISTS,
} OvStatus;


/**********************************************************************************
 *                              STATIC ARRAYS
 *********************************************************************************/

#define OV_DEFINE_ARRAY(member_type, type_name, func_prefix) \
	typedef struct { \
		size_t capacity; \
		size_t size; \
		member_type data[]; \
	}	type_name; \
  \
	type_name *func_prefix##_create(void *buffer, size_t buffer_size) { \
		if (buffer_size <= sizeof (type_name)) { \
			return NULL; \
		} \
		type_name *array = (type_name*)buffer; \
		array->capacity = (buffer_size - sizeof(type_name))/sizeof(member_type); \
		array->size = 0; \
		return array; \
	} \
	\
	OvStatus func_prefix##_add(type_name *array, member_type element) { \
		if (array->size + 1 > array->capacity) { \
			return OV_STATUS_OUT_OF_BOUNDS; \
		} \
		array->data[array->size] = element; \
		array->size += 1; \
		return OV_STATUS_OK; \
	} \
	\
	OvStatus func_prefix##_get(type_name *array, size_t index, OV_OUT member_type *element) { \
		if (index >= array->size) { \
			return OV_STATUS_OUT_OF_BOUNDS; \
		} \
		if (element != NULL) { \
			*element = array->data[index]; \
		}	\
		return OV_STATUS_OK; \
	} \
	\
	OvStatus func_prefix##_remove(type_name *array, size_t index) { \
		if (index >= array->size) { \
			return OV_STATUS_OUT_OF_BOUNDS; \
		} \
		array->data[index] = array->data[array->size-1]; \
		array->size -= 1; \
		return OV_STATUS_OK; \
	}


/**********************************************************************************
 *                             ARENA ALLOCATOR
 *********************************************************************************/

typedef struct OvArena {
	size_t capacity;
	size_t offset;
	uint8_t buffer[];
} OvArena;

OVDEF OvArena *ov_arena_create(void *buffer, size_t capacity);
OVDEF void *ov_arena_alloc_aligned(OvArena* arena, size_t size, size_t align);
OVDEF void *ov_arena_alloc(OvArena* arena, size_t size);
OVDEF void ov_arena_reset(OvArena *arena);

/**********************************************************************************
 *                         PRIORITY QUEUE 
 *********************************************************************************/

typedef struct OvPQItem {
	size_t index;
	float priority;
} OvPQItem;

typedef struct OvPQ {
	OvPQItem *items;
	size_t *positions; 
	size_t count;
	size_t capacity;
} OvPQ;


// Capacity of the heap to be able to store both the indices and their respective positions in the heap.
OVDEF OvPQ *ov_pq_create(OvArena *arena, size_t capacity);
OVDEF OvStatus ov_pq_add(OvPQ *queue, size_t index, float priority);
OVDEF OvStatus ov_pq_remove_root(OvPQ *queue, OV_OUT size_t *index); 
OVDEF bool ov_pq_contains(OvPQ *queue, size_t index);
OVDEF OvStatus ov_pq_update_priority(OvPQ *queue, size_t index, float new_priority);
OVDEF void ov_pq_clear(OvPQ *queue);
OVDEF bool ov_pq_is_empty(OvPQ *queue);

#ifdef OVERTURE_IMPLEMENTATION

/**********************************************************************************
 *                     ARENA ALLOCATOR IMPLEMENTATION
 *********************************************************************************/

uintptr_t ov_align_up(uintptr_t ptr, size_t align) {
	/* Since align is a power of two, align-1u is a mask where all low bits are set 
	   to one (0b10 -> 0b1, 0b100 -> 0b11, 0b1000 -> 0b111, etc.)
	 */
	uintptr_t mask = (uintptr_t)align - 1u;
	/* Then (ptr + mask) bumps the pointer to the next "bucket" of the powers of two,
	   it will likely overshoot, this where the logical AND with (~m), it clears the
	   low bits leaving us at the exact power of two.
	   Example: suppose ptr = 73, align = 16
	            83 + 15 = 98
	            98 & (~15) = 98 & 0b11..10000 = 96
	    This is equivalent to ptr + align - (ptr % align)
	 */
	return (ptr + mask) & (~mask);
}

OVDEF OvArena *ov_arena_create(void *buffer, size_t buffer_size) {
	if (buffer_size <= sizeof(OvArena)) {
		return NULL;
	}
	OvArena *arena = (OvArena*)buffer;
	arena->capacity = buffer_size - sizeof (OvArena);
	arena->offset = 0;
	return arena;
}

OVDEF void *ov_arena_alloc_aligned(OvArena* arena, size_t size, size_t align) {
	OV_ASSERT((align & (align-1u)) == 0); // power of two
	uintptr_t ptr = (uintptr_t)arena->buffer + (uintptr_t)arena->offset;
	size_t offset_aligned = (size_t)(ov_align_up(ptr, align) - (uintptr_t)arena->buffer);

	// I think this should not overflow?
	if (offset_aligned <= arena->capacity - size) {
		void* ptr = &arena->buffer[offset_aligned];
		arena->offset = offset_aligned + size;
		memset(ptr, 0, size);
		return ptr;
	}
	return NULL;
}

OVDEF void *ov_arena_alloc(OvArena *arena, size_t size) {
	return ov_arena_alloc_aligned(arena, size, OV_DEFAULT_ALIGNMENT);
}

OVDEF void ov_arena_reset(OvArena *arena) {
	arena->offset = 0;
}

/**********************************************************************************
 *                       PRIORITY QUEUE IMPLEMENTATION 
 *********************************************************************************/

#define OV_PQ_START_INDEX       1u  // We start indexing queue items from 1, not from 0.
#define OV_PQ_POSITION_SENTINEL 0u  // Since we're indexing from 1, 0 can be use as a sentinel value.

OVDEF OvPQ *ov_pq_create(OvArena *arena, size_t capacity) {

	OvPQ *queue = ov_arena_alloc(arena, sizeof(OvPQ));
	if (queue == NULL) {
		return NULL;
	}
	OvPQItem *items = ov_arena_alloc(arena, (capacity + OV_PQ_START_INDEX) * sizeof(OvPQItem));
	if (items == NULL) {
		return NULL;
	}
	size_t *positions = ov_arena_alloc(arena, capacity * sizeof(size_t));
	if (positions == NULL) {
		return NULL;
	}
	queue->items = items;
	queue->positions = positions;
	queue->capacity = capacity;
	ov_pq_clear(queue);
	return queue;
}

OVINTERNAL inline void ov_pq_swap(OvPQ *queue, size_t position_a, size_t position_b) {
	OvPQItem item_a = queue->items[position_a];
	OvPQItem item_b = queue->items[position_b];

	queue->items[position_a] = item_b;
	queue->items[position_b] = item_a;

	queue->positions[item_a.index] = position_b;
	queue->positions[item_b.index] = position_a;
}

OVINTERNAL void ov_pq_fix_up(OvPQ *queue, size_t position) {
	while (position > OV_PQ_START_INDEX && queue->items[position].priority < queue->items[position/2].priority) {
		ov_pq_swap(queue, position, position/2);
		position = position/2;
	}
}

OVINTERNAL void ov_pq_fix_down(OvPQ *queue, size_t position) {
	while (2*position <= queue->count) {
		size_t child = 2 * position;
		if (child < queue->count && queue->items[child].priority > queue->items[child+1].priority) {
			// Left child (2*pos) is greater than right child (2*pos+1), so we work with the smaller one.
			child += 1;
		}
		if (queue->items[position].priority <= queue->items[child].priority) {
			// If current node is less than or equal to the smaller child, we're done.
			break; 
		}
		ov_pq_swap(queue, position, child);
		position = child;
	}
}

OVDEF OvStatus ov_pq_add(OvPQ *queue, size_t index, float priority) {
	if (ov_pq_contains(queue, index)) {
		return OV_STATUS_ALREADY_EXISTS;
	}
	if (queue->count + 1 >= queue->capacity + OV_PQ_START_INDEX || index >= queue->capacity) {
		return OV_STATUS_OUT_OF_BOUNDS;
	}
	queue->count += 1;
	queue->items[queue->count] = (OvPQItem){.index = index, .priority = priority};
	queue->positions[index] = queue->count;
	ov_pq_fix_up(queue, queue->count);
	return OV_STATUS_OK;
}

OVDEF OvStatus ov_pq_remove_root(OvPQ *queue, OV_OUT size_t *index) {
	if (queue->count == 0) {
		return OV_STATUS_EMPTY;
	}
	size_t root = queue->items[1].index;

	if (index != NULL) {
		*index = root; 
	}
	ov_pq_swap(queue, 1, queue->count);
	queue->count -= 1;
	ov_pq_fix_down(queue, 1);
	// After we fixed the heap we set the root-th position to the sentinel value to indicate 
	// that the item is absent from the queue.
	queue->positions[root] = OV_PQ_POSITION_SENTINEL;
	return OV_STATUS_OK;
}

OVDEF bool ov_pq_contains(OvPQ *heap, size_t index) {
	if (index >= heap->capacity) {
		return false;
	}
	return heap->positions[index] != OV_PQ_POSITION_SENTINEL;
}

OVDEF OvStatus ov_pq_update_priority(OvPQ *queue, size_t index, float new_priority) {
	if (queue->count == 0) {
		return OV_STATUS_EMPTY;
	}
	if (index >= queue->capacity) {
		return OV_STATUS_OUT_OF_BOUNDS;
	}
	size_t position = queue->positions[index];
	float old_priority = queue->items[position].priority; 
	queue->items[position].priority = new_priority;

	if (new_priority < old_priority) {
		ov_pq_fix_up(queue, position);
	} else {
		ov_pq_fix_down(queue, position);
	}
	return OV_STATUS_OK;
}

OVDEF void ov_pq_clear(OvPQ *queue) {
	memset(queue->positions, OV_PQ_POSITION_SENTINEL, queue->capacity * sizeof(*queue->positions));
	queue->count = 0;
}

OVDEF bool ov_pq_is_empty(OvPQ *queue) {
	return queue->count == 0;
}

#endif  /* OVERTURE_IMPLEMENTATION */
#endif  /* OVERTURE_H_ */

/**********************************************************************************
 *                                REFERENCES                                      *
 **********************************************************************************
 - How I Program C by Eskil Steenberg: https://www.youtube.com/watch?v=443UNeGrFoM
 - Memory Allocation Strategies by GingerBill: 
   https://web.archive.org/web/20250628233039/https://www.gingerbill.org/series/memory-allocation-strategies/
 - Binary Heaps 
   https://web.archive.org/web/20250424173115/https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html
 - Algorithms in C, Parts 1-4: Fundamentals, Data Structures, Sorting, Searching by R. Sedgewick (book)
 **********************************************************************************/
