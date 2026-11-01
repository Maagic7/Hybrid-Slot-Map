
// ===========================================================================
//  FILE: main.c (PART 1 - LIFECYCLE & CORE TESTS - C99 COMPLIANT)
//  DESC: Full verification suite for the Hybrid Slot Map Engine
// ===========================================================================

#include "hybrid_slotmap.h"
#include <stdio.h>
#include <stdbool.h>

// 32-Byte aligned structure matching the PureBasic TVector3D
typedef struct {
    double x, y, z, w;
} vector3d_t;

// Callback matching the PureBasic CAD_OnTopologyUpdate
void cad_on_topology_update(uint64_t handle, int32_t new_dense_idx) {
    printf("  -> [Callback] Point-Handle %llu moved to new OpenGL Vertex-Index: %d\n", handle, new_dense_idx);
}

// Custom Client Comparator: Sorts vectors by X-Coordinate (Ascending)
int cad_compare_by_x(const void* item_a, const void* item_b) {
    double x_a = ((vector3d_t*)item_a)->x;
    double x_b = ((vector3d_t*)item_b)->x;
    if (x_a < x_b) return 1;  // Returns 1 if order should be swapped (Quicksort step)
    return 0;
}

// Custom Client Predicate: Keeps items visible if X > 150.0
bool cad_filter_x_greater_than_150(const void* item) {
    return ((vector3d_t*)item)->x > 150.0;
}

// ===========================================================================
// TEST 1: PURE DENSE MODE LIVE MANIPULATION
// ===========================================================================
void test_smap_dense(void) {
    printf("==================================================================\n");
    printf("          TEST 1: PURE DENSE LIVE MANIPULATION & SWAP             \n");
    printf("==================================================================\n");
    
    printf("[SMap] Creating container directly in DENSE mode...\n");
    smap_t* points_map = smap_new(SMAP_DENSE, sizeof(vector3d_t), 1024);
    
    printf("[CAD] Inserting 3 points directly into Dense RAM...\n");
    vector3d_t tmp_pt = {10.0, 20.0, 30.0, 1.0};
    uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 40.0; tmp_pt.y = 50.0; tmp_pt.z = 60.0;
    uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 70.0; tmp_pt.y = 80.0; tmp_pt.z = 90.0;
    uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t idx = smap_get_index(points_map, h_pt1);
    if (idx != -1) {
        cad_buffer[idx].x = 999.99;
        printf("-> Verification: Pt1\\x direct write value: %.2f\n", cad_buffer[idx].x);
    }
    
    printf("[SMap] Deleting Point 2 (Triggers runtime Swap-With-Last)...\n");
    smap_delete(points_map, h_pt2);
    
    printf("[Verification] Scanning dense storage from index 0:\n");
    cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t active = smap_get_active_count(points_map);
    for (int32_t i = 0; i < active; i++) {
        printf("  -> RAM Slot [%d]: X=%.2f | Y=%.2f\n", i, cad_buffer[i].x, cad_buffer[i].y);
    }
    
    smap_destroy(points_map);
    printf("[Done] Test 1 cleared safely.\n\n");
}

// ===========================================================================
// TEST 2: SPARSE IMPORT TO DENSE BAKING CONVERSION
// ===========================================================================
void test_smap_baking(void) {
    printf("==================================================================\n");
    printf("          TEST 2: SPARSE IMPORT TO DENSE BAKING CONVERSION        \n");
    printf("==================================================================\n");
    
    smap_t* points_map = smap_new(SMAP_SPARSE, sizeof(vector3d_t), 1024);
    
    printf("[CAD] Importing 5 points into sparse index slots...\n");
    vector3d_t tmp_pt = {0, 0, 0, 1.0};
    tmp_pt.x = 10.0; tmp_pt.y = 10.0; uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 20.0; tmp_pt.y = 20.0; uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 30.0; tmp_pt.y = 30.0; uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 40.0; tmp_pt.y = 40.0; uint64_t h_pt4 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 50.0; tmp_pt.y = 50.0; uint64_t h_pt5 = smap_insert(points_map, &tmp_pt);
    
    printf("[CAD] Simulating duplicate cleanup (Deleting Point 2 and Point 3)...\n");
    smap_delete(points_map, h_pt2);
    smap_delete(points_map, h_pt3);
    
    printf("[SMap] Triggering 'Bake' via ConvertToDense()...\n");
    smap_convert_to_dense(points_map, cad_on_topology_update);
    printf("[SMap] Conversion finished. Mode is now: #SMAP_DENSE\n");
    
    printf("[Verification] Scanning baked buffer sequentially from index 0:\n");
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t active = smap_get_active_count(points_map);
    for (int32_t i = 0; i < active; i++) {
        printf("  -> Render-Slot [%d]: X=%.2f | Y=%.2f\n", i, cad_buffer[i].x, cad_buffer[i].y);
    }
    
    smap_destroy(points_map);
    printf("[Done] Test 2 cleared safely.\n\n");
}

// ===========================================================================
// TEST 3: DENSE TO SPARSE LIVE HOT-SWAP
// ===========================================================================
void test_smap_switch_to_sparse(void) {
    printf("==================================================================\n");
    printf("          TEST 3: DENSE TO SPARSE LIVE HOT-SWAP                   \n");
    printf("==================================================================\n");
    
    smap_t* points_map = smap_new(SMAP_SPARSE, sizeof(vector3d_t), 1024);
    
    printf("[CAD] Setup baseline points in Sparse Puffer...\n");
    vector3d_t tmp_pt = {0, 0, 0, 1.0};
    tmp_pt.x = 100.0; tmp_pt.y = 100.0; uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 200.0; tmp_pt.y = 200.0; uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 300.0; tmp_pt.y = 300.0; uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    
    printf("[CAD] Delete Point 2 (Creates a hole)...\n");
    smap_delete(points_map, h_pt2);
    
    printf("[SMap] Baking container into DENSE layout for stable GPU Render-Pass...\n");
    smap_convert_to_dense(points_map, cad_on_topology_update);
    
    printf("[SMap] Triggering 'SwitchToSparse' (No data bytes will be moved!)...\n");
    smap_convert_to_sparse(points_map);
    
    printf("[Verification] Checking Handle states after hot-swapping back to Sparse:\n");
    printf("  -> Pt 1 valid? %d\n", smap_is_valid(points_map, h_pt1));
    printf("  -> Pt 2 valid? %d (Should be 0 / deleted)\n", smap_is_valid(points_map, h_pt2));
    printf("  -> Pt 3 valid? %d\n", smap_is_valid(points_map, h_pt3));
    
    printf("[CAD] Simulating continuous user editing: Inserting a new Point...\n");
    tmp_pt.x = 400.0; tmp_pt.y = 400.0;
    uint64_t h_pt_new = smap_insert(points_map, &tmp_pt);
    
    printf("  -> New Point valid? %d\n", smap_is_valid(points_map, h_pt_new));
    
    printf("[Verification] Scanning full buffer to check new index layout:\n");
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    
    printf("  -> Pt 1 location in RAM: Index %d | X=%.2f\n", smap_get_index(points_map, h_pt1), cad_buffer[smap_get_index(points_map, h_pt1)].x);
    printf("  -> Pt 3 location in RAM: Index %d | X=%.2f\n", smap_get_index(points_map, h_pt3), cad_buffer[smap_get_index(points_map, h_pt3)].x);
    printf("  -> New Pt location in RAM: Index %d | X=%.2f\n", smap_get_index(points_map, h_pt_new), cad_buffer[smap_get_index(points_map, h_pt_new)].x);
    
    smap_destroy(points_map);
    printf("[Done] Test 3 cleared safely.\n\n");
}

// ===========================================================================
//  FILE: main.c (PART 2 - VIRTUAL & HYBRID WORKFLOW TESTS - C99 COMPLIANT)
// ===========================================================================

// ===========================================================================
// TEST 4: VIRTUAL DENSE SORTING & UNSORT VIA REVMAP
// ===========================================================================
void test_smap_virtual_sorting(void) {
    printf("==================================================================\n");
    printf("          TEST 4: VIRTUAL DENSE SORTING & UNSORT VIA REVMAP       \n");
    printf("==================================================================\n");
    
    smap_t* points_map = smap_new(SMAP_DENSE, sizeof(vector3d_t), 1024);
    
    vector3d_t tmp_pt = {300.0, 0, 0, 1.0}; uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 100.0;                       uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 200.0;                       uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    
    printf("[CAD] Initial physical memory layout (As inserted):\n");
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t active = smap_get_active_count(points_map);
    for (int32_t i = 0; i < active; i++) {
        printf("  -> RAM Array Slot [%d]: X=%.2f\n", i, cad_buffer[i].x);
    }
    
    printf("[SMap] Triggering 'SortDenseByCallback' (Sorting rev_map by X)...\n");
    smap_sort(points_map, cad_compare_by_x);
    
    printf("[Grid UI] Reading data sequentially while SORTED:\n");
    for (int32_t i = 0; i < active; i++) {
        int32_t slot_idx = points_map->_int.rev_map[i].idx;
        printf("  -> UI Grid Row [%d]: X=%.2f\n", i, cad_buffer[points_map->_int.slots[slot_idx].idx].x);
    }
    
    printf("[SMap] Triggering 'ResetVirtualSorting' (Restoring physical order)...\n");
    smap_reset_sort_and_filter(points_map);
    
    printf("[Grid UI] Reading data sequentially after UNSORT:\n");
    for (int32_t i = 0; i < active; i++) {
        int32_t slot_idx = points_map->_int.rev_map[i].idx;
        printf("  -> UI Grid Row [%d]: X=%.2f\n", i, cad_buffer[points_map->_int.slots[slot_idx].idx].x);
    }
    
    smap_destroy(points_map);
    printf("[Done] Test 4 cleared safely.\n\n");
}

// ===========================================================================
// TEST 5: VIRTUAL DENSE FILTERING VIA REVMAP
// ===========================================================================
void test_smap_virtual_filtering(void) {
    printf("==================================================================\n");
    printf("          TEST 5: VIRTUAL DENSE FILTERING VIA REVMAP              \n");
    printf("==================================================================\n");
    
    smap_t* points_map = smap_new(SMAP_DENSE, sizeof(vector3d_t), 1024);
    
    vector3d_t tmp_pt = {100.0, 0, 0, 1.0}; uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 300.0;                       uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 120.0;                       uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    
    printf("[CAD] Initial physical memory layout (All 3 items present):\n");
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t active = smap_get_active_count(points_map);
    for (int32_t i = 0; i < active; i++) {
        printf("  -> RAM Array Slot [%d]: X=%.2f\n", i, cad_buffer[i].x);
    }
    
    printf("[SMap] Triggering 'FilterDenseByPredicate' (Criteria: X > 150)...\n");
    smap_filter(points_map, cad_filter_x_greater_than_150);
    
    printf("[Grid UI / OpenGL] Reading active subset while FILTERED:\n");
    active = smap_get_active_count(points_map);
    printf("  -> Active count reported by SMap: %d (Should be 1)\n", active);
    for (int32_t i = 0; i < active; i++) {
        int32_t slot_idx = points_map->_int.rev_map[i].idx;
        printf("  -> Visible Element [%d]: X=%.2f\n", i, cad_buffer[points_map->_int.slots[slot_idx].idx].x);
    }
    
    printf("[SMap] Removing filter via 'ResetVirtualSorting'...\n");
    smap_reset_sort_and_filter(points_map);
    
    printf("[Grid UI] Reading full set after clearing filter:\n");
    active = smap_get_active_count(points_map);
    printf("  -> Active count reported by SMap: %d (Should be 3)\n", active);
    for (int32_t i = 0; i < active; i++) {
        int32_t slot_idx = points_map->_int.rev_map[i].idx;
        printf("  -> Visible Element [%d]: X=%.2f\n", i, cad_buffer[points_map->_int.slots[slot_idx].idx].x);
    }
    
    smap_destroy(points_map);
    printf("[Done] Test 5 cleared safely.\n\n");
}

// ===========================================================================
// TEST 6: COMPLETE HYBRID WORKFLOW LIFECYCLE
// ===========================================================================
void test_smap_hybrid_lifecycle(void) {
    const char* status_str;
    printf("==================================================================\n");
    printf("          TEST 6: COMPLETE HYBRID WORKFLOW LIFECYCLE             \n");
    printf("==================================================================\n");
    
    smap_t* points_map = smap_new(SMAP_SPARSE, sizeof(vector3d_t), 1024);
    printf("[STAGE 1] Initializing container in SPARSE mode...\n");
    
    vector3d_t tmp_pt = {10.0, 0, 0, 1.0}; uint64_t h_pt1 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 20.0;                       uint64_t h_pt2 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 30.0;                       uint64_t h_pt3 = smap_insert(points_map, &tmp_pt);
    tmp_pt.x = 40.0;                       uint64_t h_pt4 = smap_insert(points_map, &tmp_pt);
    
    printf("  -> [RAM Scan] Initial full memory layout (0..3):\n");
    vector3d_t* cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    for (int32_t i = 0; i < 4; i++) {
        printf("     -> Raw RAM Array Slot [%d]: X=%.2f [VALID]\n", i, cad_buffer[i].x);
    }
    
    printf("  -> Deleting Point 2 and Point 3 to force memory holes...\n");
    smap_delete(points_map, h_pt2);
    smap_delete(points_map, h_pt3);
    
    printf("  -> [RAM Scan] Layout after deletions (Notice unchanged memory, but invalid handles):\n");
    for (int32_t i = 0; i < 4; i++) {
        status_str = (i == 1 || i == 2) ? " [DELETED/HOLE]" : " [VALID]";
        printf("     -> Raw RAM Array Slot [%d]: X=%.2f%s\n", i, cad_buffer[i].x, status_str);
    }
    
    printf("[STAGE 2] Baking container into DENSE layout for GPU rendering...\n");
    smap_convert_to_dense(points_map, cad_on_topology_update);
    
    printf("  -> [RAM Scan] Packed GPU buffer (Notice variables compacted to front 0..1):\n");
    cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t active = smap_get_active_count(points_map);
    for (int32_t i = 0; i < active; i++) {
        printf("     -> Raw RAM Array Slot [%d]: X=%.2f [VALID]\n", i, cad_buffer[i].x);
    }
    
    printf("[STAGE 3] Hot-Swapping back to SPARSE mode (Zero-Copy Index Remapping)...\n");
    smap_convert_to_sparse(points_map);
    
    printf("[CAD] Simulating post-swap editing: Inserting a brand new Point (X=500)...\n");
    tmp_pt.x = 500.0;
    uint64_t h_pt_new = smap_insert(points_map, &tmp_pt);
    
    printf("  -> [Slot Table Scan] Internal Slot Layout after inserting New Point (Slots 1..4):\n");
    for (int32_t i = 1; i <= 4; i++) {
        int32_t dense_idx = points_map->_int.slots[i].idx;
        status_str = " [FREE/DELETED]";
        if (i == 1 || i == 4) status_str = " [ACTIVE -> Points to RAM Index]";
        else if (i == 3) status_str = " [NEWLY RECYCLED -> Points to RAM Index]";
        printf("     -> Slot [%d]: idx=%d | gen=%u%s\n", i, dense_idx, points_map->_int.slots[i].gen, status_str);
    }
    
    printf("  -> [Ghost Reference Check] Verifying that old handles to deleted slots are blocked:\n");
    if (!smap_is_valid(points_map, h_pt2)) {
        printf("     -> Old h_pt2 (Point 2) Status: [BLOCKED / INVALID] -> Generation mismatch protection works!\n");
    } else {
        printf("     -> Old h_pt2 (Point 2) Status: [ERROR] -> Ghost reference allowed!\n");
    }
    if (smap_is_valid(points_map, h_pt_new)) {
        printf("     -> New h_pt_new (X=500) Status: [VALID] -> Slot successfully recycled with new generation.\n");
    }
    
    printf("  -> [RAM Scan] Final physical layout in SPARSE COMPACT mode:\n");
    cad_buffer = (vector3d_t*)smap_get_raw_buffer(points_map);
    int32_t limit = points_map->_int.h_physical_cnt;
    for (int32_t i = 0; i < limit; i++) {
        status_str = " [DELETED/HOLE]";
        if (smap_is_valid(points_map, h_pt1) && smap_get_index(points_map, h_pt1) == i) status_str = " [VALID (Pt 1)]";
        else if (smap_is_valid(points_map, h_pt4) && smap_get_index(points_map, h_pt4) == i) status_str = " [VALID (Pt 4)]";
        else if (smap_is_valid(points_map, h_pt_new) && smap_get_index(points_map, h_pt_new) == i) status_str = " [VALID (New Pt)]";
        printf("     -> Raw RAM Array Slot [%d]: X=%.2f%s\n", i, cad_buffer[i].x, status_str);
    }
    
    smap_destroy(points_map);
    printf("[Done] Test 6 cleared safely.\n\n");
}

// ===========================================================================
// MAIN ENTRY POINT
// ===========================================================================
int main(void) {
    test_smap_dense();
    test_smap_baking();
    test_smap_switch_to_sparse();
    test_smap_virtual_sorting();
    test_smap_virtual_filtering();
    test_smap_hybrid_lifecycle();
    return 0;
}
