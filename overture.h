#ifndef OVERTURE_H_ 
#define OVERTURE_H_

#ifndef OVDEF
#define OVDEF
#endif  /* OVDEF */

#ifndef OV_ASSERT
#include <assert.h>
#define OV_ASSERT(x) assert(x)
#endif  /* OV_ASSERT */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef OV_DEFAULT_ALIGNMENT
#define OV_DEFAULT_ALIGNMENT (2u*sizeof(void*))
#endif

typedef enum OvStatus {
	OV_STATUS_OK,
	OV_STATUS_BUFFER_OVERRUN,
} OvStatus;
/*
typedef struct OvStatusSize {
	OvStatus status;
	size_t value;
} OvStatusSize;
*/

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

#define OV_BINARY_HEAP_BAD_POSITION -1

typedef int (*ov_binary_heap_compare)(int index_a, int index_b, void *array);

typedef struct OvBinaryHeap {
	size_t *indices;
	size_t count;
	size_t capacity;
	ov_binary_heap_compare compare;
	void *array;  // array of values indexed by the heap.
} OvBinaryHeap;

OVDEF void ov_binary_heap_init(
		OvBinaryHeap *heap, size_t *indices, size_t capacity, ov_binary_heap_compare compare, void* array);
OVDEF OvStatus ov_binary_heap_push(OvBinaryHeap *heap, size_t index);
// OVDEF OvStatusSize ov_binary_heap_pop(OvBinaryHeap *heap); 
OVDEF OvStatus ov_binary_heap_pop(OvBinaryHeap *heap, size_t *out); 

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

OVDEF void ov_binary_heap_init(
		OvBinaryHeap *heap, size_t *indices, size_t capacity, ov_binary_heap_compare compare, void *array) {
	heap->indices = indices;
	heap->capacity = capacity;
	heap->count = 0;
	heap->compare = compare;
	heap->array = array;
}

static void ov_internal_binary_heap_percolate_up(OvBinaryHeap *heap, size_t position) {
	int index = heap->indices[position];
	for (; position > 1 && heap->compare(index, heap->indices[position/2], heap->array) < 0; position = position/2) {
		heap->indices[position] = heap->indices[position/2];
	}
	heap->indices[position] = index;
}

static void ov_internal_binary_heap_percolate_down(OvBinaryHeap *heap, size_t position) {
	int tmp = heap->indices[position];
	int child = 0;

	for (; 2*position <= heap->count; position = child) {
		child = 2*position;

		if (heap->compare(heap->indices[child], heap->indices[child+1], heap->array) > 0) {
				child += 1;
		}
		if (heap->compare(tmp, heap->indices[child], heap->array) > 0) {
				heap->indices[position] = heap->indices[child];
		} else {
			break;
		}
	}
	heap->indices[position] = tmp;
}

OVDEF OvStatus ov_binary_heap_push(OvBinaryHeap *heap, size_t index) {
	if (heap->count + 1 >= heap->capacity) {
		return OV_STATUS_BUFFER_OVERRUN;
	}
	heap->count += 1;
	heap->indices[heap->count] = index;
	ov_internal_binary_heap_percolate_up(heap, heap->count);
	return OV_STATUS_OK;
}

#if 0
OVDEF OvStatusSize ov_binary_heap_pop(OvBinaryHeap *heap) {
	if (heap->count == 0) {
		return (OvStatusSize){.status = OV_STATUS_BUFFER_OVERRUN, .value = 0};
	}
	size_t root = heap->indices[1]; 
	heap->indices[1] = heap->indices[heap->count]; // last element
	heap->count -= 1;
	ov_internal_binary_heap_percolate_down(heap, 1);
	return (OvStatusSize){.status = OV_STATUS_OK, .value = root};
}
#endif

OVDEF OvStatus ov_binary_heap_pop(OvBinaryHeap *heap, size_t *out) {
	if (heap->count == 0) {
		return OV_STATUS_BUFFER_OVERRUN;
	}
	*out = heap->indices[1]; 
	heap->indices[1] = heap->indices[heap->count]; // last element
	heap->count -= 1;
	ov_internal_binary_heap_percolate_down(heap, 1);
	return OV_STATUS_OK;
}
#endif  /* OVERTURE_IMPLEMENTATION */
#endif  /* OVERTURE_H_ */

/**********************************************************************************
 *                                REFERENCES                                      *
 **********************************************************************************
 * - How I Program C by Eskil Steenberg: https://www.youtube.com/watch?v=443UNeGrFoM
 * - Memory Allocation Strategies by GingerBill: 
 *   https://web.archive.org/web/20250628233039/https://www.gingerbill.org/series/memory-allocation-strategies/
 * - Binary Heaps (CMU course materials)
 *   https://web.archive.org/web/20250424173115/https://www.andrew.cmu.edu/course/15-121/lectures/Binary%20Heaps/heaps.html
 **********************************************************************************/
