#ifndef OVERTURE_H_
#define OVERTURE_H_

#ifndef OVDEF
#define OVDEF
#endif  /* OVDEF */

#ifndef OV_ASSERT
#include <assert.h>
#define OV_ASSERT(x) assert(x)
#endif  /* OV_ASSERT */

#include <stddef.h>
#include <stdint.h>

#ifndef OV_DEFAULT_ALIGNMENT
#define OV_DEFAULT_ALIGNMENT (2u*sizeof(void*))
#endif


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
 *                               BINARY HEAP
 *********************************************************************************/

#define OV_BINARY_HEAP_BAD_INDEX -1

typedef int (*ov_binary_heap_compare)(int a, int b, void *ctx);

typedef struct OvBinaryHeap {
	int *indices;
	size_t count;
	size_t capacity;
	ov_binary_heap_compare compare;
	void *ctx; 
} OvBinaryHeap;

OVDEF void ov_binary_heap_init(
		OvBinaryHeap *heap, int *indices, size_t capacity, ov_binary_heap_compare compare, void* ctx);
OVDEF void ov_binary_heap_insert(OvBinaryHeap *heap, int idx);
OVDEF int ov_binary_heap_delete_root(OvBinaryHeap *heap); 

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
//TODO: Should we align at the initialization or do we require that it's a fresh alloc?	
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
		OvBinaryHeap *heap, int *indices, size_t capacity, ov_binary_heap_compare compare, void *ctx) {
	heap->indices = indices;
	heap->capacity = capacity;
	heap->count = 0;
	heap->compare = compare;
	heap->ctx = ctx;
}

OVDEF void ov_binary_heap_insert(OvBinaryHeap *heap, int idx) {
	if (heap->count + 1 >= heap->capacity) {
		return;
	}
	heap->count += 1;
	size_t pos = heap->count;
	for (; pos > 1 && heap->compare(idx, heap->indices[pos/2], heap->ctx) < 0; pos =	pos/2) {
		heap->indices[pos] = heap->indices[pos/2];
	}
	heap->indices[pos] = idx;
}

OVDEF int ov_binary_heap_delete_root(OvBinaryHeap *heap) {
	if (heap->count == 0) {
		return OV_BINARY_HEAP_BAD_INDEX;
	}
	int root = heap->indices[1]; 
	int tmp = heap->indices[heap->count]; // last element
	heap->count -= 1;
	
	size_t k = 1;
	size_t child = 0;
	// TODO: bounds, should it be child instead 2*k?
	for (; 2*k <= heap->count; k = child) {
		child = 2*k;

		if (heap->compare(heap->indices[child], heap->indices[child+1], heap->ctx) > 0) {
				child += 1;
		}
		if (heap->compare(tmp, heap->indices[child], heap->ctx) > 0) {
				heap->indices[k] = heap->indices[child];
		} else {
			break;
		}
	}
	heap->indices[k] = tmp;
	return root;
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
