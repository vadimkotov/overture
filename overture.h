#ifndef OVERTURE_H_ 
#define OVERTURE_H_

#ifndef OVDEF
#define OVDEF
#endif  /* OVDEF */

#ifndef OVINTERNAL
#define OVINTERNAL static
#endif /* OVINTERNAL */

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

typedef enum OvStatus {
	OV_STATUS_OK,
	OV_STATUS_EMPTY,
	OV_STATUS_OUT_OF_BOUNDS,
} OvStatus;

/**********************************************************************************
 *                             ARENA ALLOCATOR
 *********************************************************************************/
typedef struct OvArena {
	uint8_t* buffer;
	size_t capacity;
	size_t offset;
} OvArena;

OVDEF void ov_arena_init(OvArena* arena, uint8_t *buffer, size_t size);
OVDEF void *ov_arena_alloc_aligned(OvArena* arena, size_t size, uintptr_t align);
OVDEF void *ov_arena_alloc(OvArena* arena, size_t size);


/**********************************************************************************
 *                          INDEXED BINARY HEAP
 *********************************************************************************/

// NOTE: We add 1u to capacity because the heap positions start from 1.
#define OV_HEAP_GET_ALLOC_SIZE_BYTES(capacity) ( ((capacity + 1u) * sizeof (size_t)) * 2u )
#define OV_HEAP_POSITION_SENTINEL 0  // We start indexing from 1, so position 0 should never be taken.

// Function pointer to compare elements indexed by the heap
typedef int (*ov_heap_compare)(size_t index_a, size_t index_b, void *context);

typedef struct OvHeap {
	size_t *indices;
	size_t *positions; // Items' positions in the heap for reverse lookup.
										 // Lengths of indices and position must be the same!
	size_t count;
	size_t capacity;
	ov_heap_compare compare;
	void *context;  // Array (or other container) of values indexed by the heap.
} OvHeap;

// NOTE: Use OV_HEAP_GET_ALLOC_SIZE_BYTES(capacity) macro to allocate the buffer as it must be twice the
// capacity of the heap to be able to store both the indices and their respective positions in the heap.
OVDEF void ov_heap_init(OvHeap *heap, size_t *buffer, size_t capacity, ov_heap_compare compare, void *context);
OVDEF OvStatus ov_heap_add(OvHeap *heap, size_t index);
OVDEF OvStatus ov_heap_remove_root(OvHeap *heap, size_t *out); 
OVDEF bool ov_heap_contains(OvHeap *heap, size_t index);
OVDEF void ov_heap_increase_priority(OvHeap *heap, size_t index);
OVDEF void ov_heap_clear(OvHeap *heap);

#ifdef OVERTURE_IMPLEMENTATION

/**********************************************************************************
 *                     ARENA ALLOCATOR IMPLEMENTATION
 *********************************************************************************/

uintptr_t ov_align_up(uintptr_t ptr, size_t align) {
	/* Since align is a power of two, align-1u is a mask where all low bits are set 
	 * to one (0b10 -> 0b1, 0b100 -> 0b11, 0b1000 -> 0b111, etc.)
	 */
	uintptr_t mask = (uintptr_t)align - 1u;
	/* Then (ptr + mask) bumps the pointer to the next "bucket" of the powers of two,
	 * it will likely overshoot, this where the logical AND with (~m), it clears the
	 * low bits leaving us at the exact power of two.
	 * Example: suppose ptr = 73, align = 16
	 *          83 + 15 = 98
	 *          98 & (~15) = 98 & 0b11..10000 = 96
	 *  This is equivalent to ptr + align - (ptr % align)
	 */
	return (ptr + mask) & (~mask);
}
OVDEF void ov_arena_init(OvArena* arena, uint8_t* buffer, size_t size) {
	arena->buffer = buffer;
	arena->capacity = size;
	arena->offset = 0;
}

OVDEF void *ov_arena_alloc_aligned(OvArena* arena, size_t size, size_t align) {
	OV_ASSERT((align & (align-1u)) == 0); // power of two
	uintptr_t ptr = (uintptr_t)arena->buffer + (uintptr_t)arena->offset;
	size_t offset_aligned = (size_t)(ov_align_up(ptr, align) - (uintptr_t)arena->buffer);

	if (offset_aligned + size <= arena->capacity) {
		void* ptr = &arena->buffer[offset_aligned];
		arena->offset = offset_aligned + size;
		return ptr;
	}
	return NULL;
}

OVDEF void* ov_arena_alloc(OvArena* arena, size_t size) {
	return ov_arena_alloc_aligned(arena, size, OV_DEFAULT_ALIGNMENT);
}


/**********************************************************************************
 *                        BINARY HEAP IMPLEMENTATION
 *********************************************************************************/

OVDEF void ov_heap_init(
		OvHeap *heap, size_t *buffer, size_t capacity, ov_heap_compare compare, void *context) {
	heap->indices = buffer;
	heap->positions = &buffer[capacity + 1];
	heap->capacity = capacity + 1;
	heap->count = 0;
	heap->compare = compare;
	heap->context = context;

	ov_heap_clear(heap);
}

OVINTERNAL inline void ov_heap_swap(OvHeap *heap, size_t position_a, size_t position_b) {
	size_t index_a = heap->indices[position_a];
	size_t index_b = heap->indices[position_b];

	heap->indices[position_a] = index_b;
	heap->indices[position_b] = index_a;

	heap->positions[index_a] = position_b;
	heap->positions[index_b] = position_a;
}

OVINTERNAL void ov_heap_fix_up(OvHeap *heap, size_t position) {
	while (position > 1 && heap->compare(heap->indices[position], heap->indices[position/2], heap->context) < 0) {
		ov_heap_swap(heap, position, position/2);
		position = position/2;
	}
}

OVINTERNAL void ov_heap_fix_down(OvHeap *heap, size_t position) {
	while (2*position <= heap->count) {
		size_t child = 2 * position;
		if (child < heap->count && heap->compare(heap->indices[child], heap->indices[child+1], heap->context) > 0) {
			// Left child (2*pos) is greater than right child (2*pos+1), so we work with the smaller one.
			child += 1;
		}
		if (heap->compare(heap->indices[position], heap->indices[child], heap->context) <= 0) { 
			// If current node is less than or equal to the smaller child, we're done.
			break; 
		}
		ov_heap_swap(heap, position, child);
		position = child;
	}
}

OVDEF OvStatus ov_heap_add(OvHeap *heap, size_t index) {
	// TODO: check if index already exists!
	if (heap->count + 1 >= heap->capacity) {
		return OV_STATUS_OUT_OF_BOUNDS;
	}
	heap->count += 1;
	heap->indices[heap->count] = index;
	heap->positions[index] = heap->count;
	ov_heap_fix_up(heap, heap->count);
	return OV_STATUS_OK;
}

OVDEF OvStatus ov_heap_remove_root(OvHeap *heap, size_t *out) {
	if (heap->count == 0) {
		return OV_STATUS_EMPTY;
	}
	size_t root = heap->indices[1];

	if (out != NULL) {
		*out = root; 
	}
	ov_heap_swap(heap, 1, heap->count);
	heap->count -= 1;
	ov_heap_fix_down(heap, 1);
	// After we fixed the heap we set the root-th position to the sentinel value to indicate 
	// that the item is absent from the queue.
	heap->positions[root] = OV_HEAP_POSITION_SENTINEL;
	return OV_STATUS_OK;
}

OVDEF bool ov_heap_contains(OvHeap *heap, size_t index) {
	// We added +1 to the initial capacity because indices start from 1 as opposed to 0
	// however the positions array is still 0..<capacity. So we perform a stricter check.
	if (index >= heap->capacity - 1) {
		return false;
	}
	return heap->positions[index] != OV_HEAP_POSITION_SENTINEL;
}

OVDEF void ov_heap_increase_priority(OvHeap *heap, size_t index) {
	ov_heap_fix_up(heap, heap->positions[index]);
}

OVDEF void ov_heap_clear(OvHeap *heap) {
	memset(heap->positions, OV_HEAP_POSITION_SENTINEL, heap->capacity * sizeof(size_t));
	heap->count = 0;
}

#endif  /* OVERTURE_IMPLEMENTATION */
#endif  /* OVERTURE_H_ */

/**********************************************************************************
 *                                REFERENCES                                      *
 **********************************************************************************
 * - How I Program C by Eskil Steenberg: https://www.youtube.com/watch?v=443UNeGrFoM
 * - Memory Allocation Strategies by GingerBill: 
 *   https://web.archive.org/web/20250628233039/https://www.gingerbill.org/series/memory-allocation-strategies/
 * - Binary Heaps 
 *   https://web.archive.org/web/20250424173115/https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html
 * - Algorithms in C, Parts 1-4: Fundamentals, Data Structures, Sorting, Searching by R. Sedgewick (book)
 **********************************************************************************/
