
// ===========================================================================
//  FILE: hybrid_slotmap.c (PART 2 - MEMORY & LIFECYCLE - C99 COMPLIANT)
// ===========================================================================

#include "hybrid_slotmap.h"
#include <stdlib.h>
#include <string.h>

// PRIVATE ROUTINE: Resizes the handle pool and re-aligns the data block
static bool _smap_grow_pool(smap_t* smap) {
    int32_t v_new_max = 0;
    size_t v_total_size = 0;
    size_t v_current_size = 0;
    void* p_new_raw = NULL;
    uint8_t* p_new_aligned = NULL;
    size_t v_align_bound = 64; // 64-Byte boundary for AVX-512 and Cache-Lines
    int32_t i = 0;

    if (!smap) return false;

    v_new_max = (smap->_int.h_max == 0) ? 16 : smap->_int.h_max * 2;

    // Allocate a larger raw buffer with padding for alignment
    v_total_size = (v_new_max * smap->item_byte_size) + v_align_bound;
    p_new_raw = malloc(v_total_size);
    if (!p_new_raw) return false;

    // Align the address to the next 64-byte boundary
    p_new_aligned = (uint8_t*)(((uintptr_t)p_new_raw + (v_align_bound - 1)) & ~(v_align_bound - 1));

    // Migrate old data if it exists
    if (smap->data && smap->_int.h_physical_cnt > 0) {
        v_current_size = smap->_int.h_max * smap->item_byte_size;
        memcpy(p_new_aligned, smap->data, v_current_size);
        free(smap->_int.raw_buffer);
    }

    smap->_int.raw_buffer = p_new_raw;
    smap->data = p_new_aligned;

    // Resize tracking slot tables using safe native realloc pointers
    smap_handle_t* p_new_slots = realloc(smap->_int.slots, (v_new_max + 1) * sizeof(smap_handle_t));
    if (!p_new_slots) return false;
    smap->_int.slots = p_new_slots;

    if (smap->mode == SMAP_DENSE) {
        smap_handle_t* p_new_rev = realloc(smap->_int.rev_map, (v_new_max + 1) * sizeof(smap_handle_t));
        if (!p_new_rev) return false;
        smap->_int.rev_map = p_new_rev;
    }

    // Initialize newly formed vacant slots and link them into the free-list
    for (i = smap->_int.h_max + 1; i <= v_new_max - 1; i++) {
        smap->_int.slots[i].idx = i + 1;
        smap->_int.slots[i].gen = 1;
    }

    smap->_int.slots[v_new_max].idx = smap->_int.h_next_free;
    smap->_int.slots[v_new_max].gen = 1;
    smap->_int.h_next_free = smap->_int.h_max + 1;
    smap->_int.h_max = v_new_max;

    return true;
}

// CONSTRUCTOR: Allocates and setups the continuous hybrid slot container
smap_t* smap_new(int32_t mode, int32_t item_byte_size, int32_t initial_items) {
    smap_t* smap = NULL;
    size_t v_total_size = 0;
    size_t v_align_bound = 64;
    int32_t i = 0;

    if (initial_items <= 0) initial_items = 16;
    if (item_byte_size <= 0) return NULL;

    smap = (smap_t*)malloc(sizeof(smap_t));
    if (!smap) return NULL;

    smap->mode = mode;
    smap->item_byte_size = item_byte_size;
    smap->_int.h_max = initial_items;
    smap->_int.h_cnt = 0;
    smap->_int.h_physical_cnt = 0;
    smap->_int.h_next_free = 1;

    v_total_size = (initial_items * item_byte_size) + v_align_bound;
    smap->_int.raw_buffer = malloc(v_total_size);
    if (!smap->_int.raw_buffer) {
        free(smap);
        return NULL;
    }

    smap->data = (uint8_t*)(((uintptr_t)smap->_int.raw_buffer + (v_align_bound - 1)) & ~(v_align_bound - 1));

    smap->_int.slots = (smap_handle_t*)malloc((initial_items + 1) * sizeof(smap_handle_t));
    smap->_int.rev_map = (smap_handle_t*)malloc(((mode == SMAP_DENSE ? initial_items : 0) + 1) * sizeof(smap_handle_t));

    for (i = 1; i <= initial_items - 1; i++) {
        smap->_int.slots[i].idx = i + 1;
        smap->_int.slots[i].gen = 1;
    }
    smap->_int.slots[initial_items].idx = -1;
    smap->_int.slots[initial_items].gen = 1;

    return smap;
}

// DESTRUCTOR: Clears all heap allocations safely using base raw pointers
void smap_destroy(smap_t* smap) {
    if (!smap) return;
    if (smap->_int.raw_buffer) free(smap->_int.raw_buffer);
    if (smap->_int.slots) free(smap->_int.slots);
    if (smap->_int.rev_map) free(smap->_int.rev_map);
    free(smap);
}

// ===========================================================================
//  FILE: hybrid_slotmap.c (PART 3 - CORE CRUD & VALIDATION - C99 COMPLIANT)
// ===========================================================================

// PUBLIC ROUTINE: Inserts a new record and forces permanent compaction
uint64_t smap_insert(smap_t* smap, const void* data_address) {
    int32_t v_slot_idx = 0;
    int32_t v_dense_idx = 0;
    uint8_t* p_target_address = NULL;
    smap_handle_t v_return_handle = { .h = 0LL };

    if (!smap || !data_address) return 0LL;

    if (smap->_int.h_next_free == -1) {
        if (!_smap_grow_pool(smap)) return 0LL;
    }

    // Pop the next available generational slot from the free-list
    v_slot_idx = smap->_int.h_next_free;
    smap->_int.h_next_free = smap->_int.slots[v_slot_idx].idx;

    // HYBRID-FIX: New items are ALWAYS appended to the true physical end
    // of the compact RAM block, preserving cache density in both modes.
    v_dense_idx = smap->_int.h_physical_cnt;

    p_target_address = smap->data + (v_dense_idx * smap->item_byte_size);
    memcpy(p_target_address, data_address, smap->item_byte_size);

    smap->_int.slots[v_slot_idx].idx = v_dense_idx;

    if (smap->mode == SMAP_DENSE) {
        smap->_int.rev_map[v_dense_idx].idx = v_slot_idx;
        smap->_int.rev_map[v_dense_idx].gen = smap->_int.slots[v_slot_idx].gen;
    }

    smap->_int.h_cnt++;
    smap->_int.h_physical_cnt++;

    v_return_handle.idx = v_slot_idx;
    v_return_handle.gen = smap->_int.slots[v_slot_idx].gen;
    return v_return_handle.h;
}

// PUBLIC ROUTINE: Removes items and triggers Swap-With-Last compaction
bool smap_delete(smap_t* smap, uint64_t handle) {
    int32_t v_slot_idx = 0;
    int32_t v_dense_idx_to_delete = 0;
    int32_t v_last_dense_idx = 0;
    int32_t v_moved_slot_idx = 0;
    uint8_t* p_source = NULL;
    uint8_t* p_target = NULL;
    smap_handle_t v_handle = { .h = handle };

    if (!smap || handle == 0LL) return false;

    v_slot_idx = v_handle.idx;

    if (v_slot_idx <= 0 || v_slot_idx > smap->_int.h_max) return false;
    if (smap->_int.slots[v_slot_idx].gen != v_handle.gen) return false;

    v_dense_idx_to_delete = smap->_int.slots[v_slot_idx].idx;

    if (smap->mode == SMAP_DENSE) {
        v_last_dense_idx = smap->_int.h_cnt - 1;

        if (v_dense_idx_to_delete < v_last_dense_idx) {
            p_target = smap->data + (v_dense_idx_to_delete * smap->item_byte_size);
            p_source = smap->data + (v_last_dense_idx * smap->item_byte_size);
            memcpy(p_target, p_source, smap->item_byte_size);

            v_moved_slot_idx = smap->_int.rev_map[v_last_dense_idx].idx;
            smap->_int.slots[v_moved_slot_idx].idx = v_dense_idx_to_delete;
            smap->_int.rev_map[v_dense_idx_to_delete] = smap->_int.rev_map[v_last_dense_idx];
        }

        smap->_int.rev_map[v_last_dense_idx].idx = 0;
        smap->_int.rev_map[v_last_dense_idx].gen = 0;
    } else {
        // Optional: Clear storage block for clean footprint in sparse mode
        p_target = smap->data + (v_dense_idx_to_delete * smap->item_byte_size);
        memset(p_target, 0, smap->item_byte_size);
    }

    // Invalidate old references by incrementing the generational barrier
    smap->_int.slots[v_slot_idx].gen++;
    smap->_int.slots[v_slot_idx].idx = smap->_int.h_next_free;
    smap->_int.h_next_free = v_slot_idx;

    smap->_int.h_cnt--;
    smap->_int.h_physical_cnt--;
    return true;
}

// PUBLIC ROUTINE: Validates handles in O(1) protecting against ghost lookup
bool smap_is_valid(smap_t* smap, uint64_t handle) {
    int32_t v_slot_idx = 0;
    int32_t v_dense_idx = 0;
    smap_handle_t v_handle = { .h = handle };

    if (!smap || handle == 0LL) return false;

    v_slot_idx = v_handle.idx;

    if (v_slot_idx <= 0 || v_slot_idx > smap->_int.h_max) return false;
    if (smap->_int.slots[v_slot_idx].gen != v_handle.gen) return false;

    v_dense_idx = smap->_int.slots[v_slot_idx].idx;

    if (smap->mode == SMAP_DENSE) {
        if (v_dense_idx >= 0 && v_dense_idx < smap->_int.h_cnt) {
            if (smap->_int.rev_map[v_dense_idx].idx == v_slot_idx) {
                return true;
            }
        }
    } else {
        // HYBRID-SPARSE: Validates the dense layout index boundary [0..h_physical_cnt-1]
        if (v_dense_idx >= 0 && v_dense_idx < smap->_int.h_physical_cnt) {
            return true;
        }
    }
    return false;
}

// ===========================================================================
//  FILE: hybrid_slotmap.c (PART 4 - HOT-SWAPPING & LOOKUPS - C99 COMPLIANT)
// ===========================================================================

// PUBLIC ROUTINE: Delivers the RAW memory data address of a specific item
void* smap_get_ptr(smap_t* smap, uint64_t handle) {
    int32_t v_slot_idx = 0;
    smap_handle_t v_handle = { .h = handle };

    if (!smap || handle == 0LL) return NULL;

    v_slot_idx = v_handle.idx;
    if (v_slot_idx <= 0 || v_slot_idx > smap->_int.h_max) return NULL;
    if (smap->_int.slots[v_slot_idx].gen != v_handle.gen) return NULL;

    return smap->data + (smap->_int.slots[v_slot_idx].idx * smap->item_byte_size);
}

// PUBLIC ROUTINE: Delivers the active linear array index [0..Items]
int32_t smap_get_index(smap_t* smap, uint64_t handle) {
    int32_t v_slot_idx = 0;
    smap_handle_t v_handle = { .h = handle };

    if (!smap || handle == 0LL) return -1;

    v_slot_idx = v_handle.idx;
    if (v_slot_idx <= 0 || v_slot_idx > smap->_int.h_max) return -1;
    if (smap->_int.slots[v_slot_idx].gen != v_handle.gen) return -1;

    return smap->_int.slots[v_slot_idx].idx;
}

// PUBLIC ROUTINE: Converts a standard sparse map into a dense contiguous layout
bool smap_convert_to_dense(smap_t* smap, smap_moved_f callback) {
    int32_t v_slot_idx = 0;
    int32_t v_current_dense_idx = 0;
    int32_t v_old_dense_idx = 0;
    int32_t v_next_free = 0;
    uint8_t* p_source = NULL;
    uint8_t* p_target = NULL;
    smap_handle_t v_tmp_handle = { .h = 0LL };

    if (!smap || smap->mode == SMAP_DENSE) return false;

    smap->_int.rev_map = (smap_handle_t*)malloc((smap->_int.h_max + 1) * sizeof(smap_handle_t));
    if (!smap->_int.rev_map) return false;

    bool* v_is_free_slot = (bool*)calloc(smap->_int.h_max + 1, sizeof(bool));
    if (!v_is_free_slot) return false;

    v_next_free = smap->_int.h_next_free;
    while (v_next_free >= 1 && v_next_free <= smap->_int.h_max) {
        v_is_free_slot[v_next_free] = true;
        v_next_free = smap->_int.slots[v_next_free].idx;
    }

    for (v_slot_idx = 1; v_slot_idx <= smap->_int.h_max; v_slot_idx++) {
        if (!v_is_free_slot[v_slot_idx]) {
            v_old_dense_idx = smap->_int.slots[v_slot_idx].idx;

            if (v_old_dense_idx != v_current_dense_idx) {
                p_source = smap->data + (v_old_dense_idx * smap->item_byte_size);
                p_target = smap->data + (v_current_dense_idx * smap->item_byte_size);
                memcpy(p_target, p_source, smap->item_byte_size);
            }

            smap->_int.slots[v_slot_idx].idx = v_current_dense_idx;
            smap->_int.rev_map[v_current_dense_idx].idx = v_slot_idx;
            smap->_int.rev_map[v_current_dense_idx].gen = smap->_int.slots[v_slot_idx].gen;

            if (callback) {
                v_tmp_handle.idx = v_slot_idx;
                v_tmp_handle.gen = smap->_int.slots[v_slot_idx].gen;
                callback(v_tmp_handle.h, v_current_dense_idx);
            }

            v_current_dense_idx++;
        }
    }

    smap->_int.h_cnt = v_current_dense_idx;
    
    // CRITICAL HYBRID INVARIANT RESET: Syncs the physical limit with compaction layout
    smap->_int.h_physical_cnt = v_current_dense_idx;
    
    smap->mode = SMAP_DENSE;

    free(v_is_free_slot);
    return true;
}

// PUBLIC ROUTINE: Converts a dense map back to sparse mode (Zero-Copy)
bool smap_convert_to_sparse(smap_t* smap) {
    int32_t v_dense_idx = 0;
    int32_t v_slot_idx = 0;

    if (!smap || smap->mode == SMAP_SPARSE) return false;

    // CRITICAL OPTIMIZATION: Zero data bytes are moved!
    // Vectors remain frozen linearly inside RAM. We update slot links via rev_map.
    for (v_dense_idx = 0; v_dense_idx < smap->_int.h_cnt; v_dense_idx++) {
        v_slot_idx = smap->_int.rev_map[v_dense_idx].idx;
        if (v_slot_idx > 0) {
            smap->_int.slots[v_slot_idx].idx = v_dense_idx;
        }
    }

    if (smap->_int.rev_map) free(smap->_int.rev_map);
    smap->_int.rev_map = (smap_handle_t*)malloc(2 * sizeof(smap_handle_t));

    smap->mode = SMAP_SPARSE;
    smap->_int.h_cnt = smap->_int.h_physical_cnt;

    return true;
}

// ===========================================================================
//  FILE: hybrid_slotmap.c (PART 5 - SORTING & FILTERING - C99 COMPLIANT)
// ===========================================================================

// Private helper: Swaps two reverse-map slots in-place
static void _smap_sort_swap(smap_t* smap, int32_t index_a, int32_t index_b) {
    smap_handle_t tmp = smap->_int.rev_map[index_a];
    smap->_int.rev_map[index_a] = smap->_int.rev_map[index_b];
    smap->_int.rev_map[index_b] = tmp;
}

// PRIVATE ROUTINE: Internal recursive QuickSort engine for the reverse map
static void _smap_quicksort(smap_t* smap, int32_t low, int32_t high, smap_compare_f compare_func) {
    int32_t i = low;
    int32_t j = high;
    int32_t v_pivot_idx = low + (high - low) / 2;
    int32_t v_slot_pivot = smap->_int.rev_map[v_pivot_idx].idx;
    const void* p_item_pivot = smap->data + (smap->_int.slots[v_slot_pivot].idx * smap->item_byte_size);

    if (low < high) {
        do {
            int32_t v_slot_i = smap->_int.rev_map[i].idx;
            const void* p_item_i = smap->data + (smap->_int.slots[v_slot_i].idx * smap->item_byte_size);
            while (compare_func(p_item_i, p_item_pivot) > 0) {
                i++;
                v_slot_i = smap->_int.rev_map[i].idx;
                p_item_i = smap->data + (smap->_int.slots[v_slot_i].idx * smap->item_byte_size);
            }

            int32_t v_slot_j = smap->_int.rev_map[j].idx;
            const void* p_item_j = smap->data + (smap->_int.slots[v_slot_j].idx * smap->item_byte_size);
            while (compare_func(p_item_pivot, p_item_j) > 0) {
                j--;
                v_slot_j = smap->_int.rev_map[j].idx;
                p_item_j = smap->data + (smap->_int.slots[v_slot_j].idx * smap->item_byte_size);
            }

            if (i <= j) {
                _smap_sort_swap(smap, i, j);
                i++;
                j--;
            }
        } while (i <= j);

        if (low < j)  _smap_quicksort(smap, low, j, compare_func);
        if (i < high) _smap_quicksort(smap, i, high, compare_func);
    }
}

// PUBLIC ROUTINE: Sorts the virtual layout order via an O(N log N) QuickSort
bool smap_sort(smap_t* smap, smap_compare_f compare_func) {
    int32_t v_active_count = 0;

    if (!smap || smap->mode == SMAP_SPARSE || !compare_func) return false;

    v_active_count = smap->_int.h_cnt;
    if (v_active_count <= 1) return true;

    _smap_quicksort(smap, 0, v_active_count - 1, compare_func);
    return true;
}

// PUBLIC ROUTINE: Resets all virtual sorting and filtering modifications
bool smap_reset_sort_and_filter(smap_t* smap) {
    int32_t v_dense_idx = 0;
    int32_t v_slot_idx = 0;

    if (!smap || smap->mode == SMAP_SPARSE) return false;

    // BUGFIX FIXED: Only iterates over elements physically stored in RAM
    for (v_dense_idx = 0; v_dense_idx < smap->_int.h_physical_cnt; v_dense_idx++) {
        for (v_slot_idx = 1; v_slot_idx <= smap->_int.h_max; v_slot_idx++) {
            if (smap->_int.slots[v_slot_idx].idx == v_dense_idx) {
                smap->_int.rev_map[v_dense_idx].idx = v_slot_idx;
                smap->_int.rev_map[v_dense_idx].gen = smap->_int.slots[v_slot_idx].gen;
                break;
            }
        }
    }
    smap->_int.h_cnt = smap->_int.h_physical_cnt;
    return true;
}

// PUBLIC ROUTINE: Filters the virtual layout visibility without moving RAM bytes
bool smap_filter(smap_t* smap, smap_filter_f filter_func) {
    int32_t v_slot_idx = 0;
    int32_t v_dense_idx = 0;
    int32_t v_match_count = 0;
    int32_t v_original_count = 0;
    const void* p_item = NULL;

    if (!smap || smap->mode == SMAP_SPARSE || !filter_func) return false;

    smap_handle_t* p_new_rev = realloc(smap->_int.rev_map, (smap->_int.h_max + 1) * sizeof(smap_handle_t));
    if (!p_new_rev) return false;
    smap->_int.rev_map = p_new_rev;

    for (v_slot_idx = 1; v_slot_idx <= smap->_int.h_max; v_slot_idx++) {
        v_dense_idx = smap->_int.slots[v_slot_idx].idx;
        if (v_dense_idx >= 0 && v_dense_idx < smap->_int.h_physical_cnt) {
            smap->_int.rev_map[v_dense_idx].idx = v_slot_idx;
            smap->_int.rev_map[v_dense_idx].gen = smap->_int.slots[v_slot_idx].gen;
        }
    }

    v_original_count = smap->_int.h_physical_cnt;

    for (v_dense_idx = 0; v_dense_idx < v_original_count; v_dense_idx++) {
        v_slot_idx = smap->_int.rev_map[v_dense_idx].idx;

        if (v_slot_idx > 0) {
            p_item = smap->data + (smap->_int.slots[v_slot_idx].idx * smap->item_byte_size);

            if (filter_func(p_item)) {
                smap->_int.rev_map[v_match_count] = smap->_int.rev_map[v_dense_idx];
                v_match_count++;
            }
        }
    }

    for (v_dense_idx = v_match_count; v_dense_idx < v_original_count; v_dense_idx++) {
        smap->_int.rev_map[v_dense_idx].idx = 0;
        smap->_int.rev_map[v_dense_idx].gen = 0;
    }

    smap->_int.h_cnt = v_match_count;
    return true;
}
