// ===========================================================================
//  FILE: hybrid_slotmap.h (PUBLIC INTERFACE - C99 COMPLIANT)
//  NAME: Hybrid Slot Map Engine (SMAP)
//  DESC: High-performance generational index mapping data structure.
//        Bridges stable 64-bit handles with contiguous, gapless arrays.
//  LICENSE: MIT
// ===========================================================================

#ifndef HYBRID_SLOTMAP_H
#define HYBRID_SLOTMAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SMAP_SPARSE 0
#define SMAP_DENSE  1

// 64-bit Pack alignment for optimal hardware register mapping
#pragma pack(push, 1)
typedef struct {
    union {
        uint64_t h;             // Fully combined stable 64-bit Handle Token
        struct {
            int32_t idx;        // Dense RAM array index (or next free slot)
            uint32_t gen;       // Generation safety counter
        };
    };
} smap_handle_t;

typedef struct {
    int32_t h_next_free;        // Head of the handle free-list chain
    int32_t h_cnt;              // Active/Visible item count (virtual boundary)
    int32_t h_physical_cnt;     // Real physical item count packed in RAM
    int32_t h_max;              // Maximum allocated capacity limit
    void* raw_buffer;           // Unaligned base OS pointer for memory tracking
    smap_handle_t* slots;       // Dynamic sparse lookup array
    smap_handle_t* rev_map;     // Dynamic reverse indirection array
} smap_internal_t;

typedef struct {
    int32_t mode;               // SMAP_SPARSE or SMAP_DENSE
    int32_t item_byte_size;     // Size in bytes of a single data element
    uint8_t* data;              // Aligned base address of the packed data RAM
    smap_internal_t _int;       // Internal management structures
} smap_t;
#pragma pack(pop)

// Function pointer prototypes for sorting and filtering policies
typedef int  (*smap_compare_f)(const void* item_a, const void* item_b);
typedef bool (*smap_filter_f)(const void* item);
typedef void (*smap_moved_f)(uint64_t handle, int32_t new_dense_idx);

// --- Core public API ---
smap_t*  smap_new(int32_t mode, int32_t item_byte_size, int32_t initial_items);
void     smap_destroy(smap_t* smap);

uint64_t smap_insert(smap_t* smap, const void* data_address);
bool     smap_delete(smap_t* smap, uint64_t handle);
void*    smap_get_ptr(smap_t* smap, uint64_t handle);
int32_t  smap_get_index(smap_t* smap, uint64_t handle);
bool     smap_is_valid(smap_t* smap, uint64_t handle);

// --- Morphing & Hot-Swapping ---
bool     smap_convert_to_dense(smap_t* smap, smap_moved_f callback);
bool     smap_convert_to_sparse(smap_t* smap);

// --- Virtual Sorting & Mask-Free Filtering ---
bool     smap_sort(smap_t* smap, smap_compare_f compare_func);
bool     smap_filter(smap_t* smap, smap_filter_f filter_func);
bool     smap_reset_sort_and_filter(smap_t* smap);

// --- Acceleration Inspectors ---
static inline void*   smap_get_raw_buffer(smap_t* smap) { return smap ? smap->data : NULL; }
static inline int32_t smap_get_active_count(smap_t* smap) { return smap ? smap->_int.h_cnt : 0; }

#endif // HYBRID_SLOTMAP_H
