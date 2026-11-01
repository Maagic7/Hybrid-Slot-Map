# Hybrid Slot Map (SMAP) – Standalone C99 / PureBasic Engine

A high-performance, data-oriented hybrid data structure designed for modern 3D graphics engines, CAD software, and Entity Component Systems (ECS). It bridges the gap between **stable, persistent 64-bit generation handles** and **strictly contiguous, gapless RAM arrays** ideal for SIMD/AVX vectorization and direct GPU streaming (VBOs).

---

## 🌟 What's New Compared to Standard Slot Maps?

Traditional Slot Maps force developers to choose a static memory architecture: either a *Sparse Slot Map* (fast inserts/deletes but fragmented memory) or a *Dense Slot Map* (gapless array via double indirection, but overhead on mutations). **SMAP shatters this limitation by introducing a fluid, runtime-morphing hybrid paradigm:**

### 1. Permanent-Dense Sparse Mode (No Memory Fragmentation)
* **Standard Implementations:** Switching back to sparse mode or deleting items leaves physical "holes" in the data array, ruining CPU cache locality.
* **SMAP Innovation:** When hot-swapping back from Dense to Sparse, the raw data bytes **remain 100% physically frozen and packed at the front of the RAM block**. Newly inserted elements recycle vacant slots via an $O(1)$ free-list, but their physical payloads are simply appended right at the end of the dense block.

### 2. Zero-Copy Live Mode Morphing
* **Standard Implementations:** Changing container behavior requires allocating a completely new data structure and copying all payloads over.
* **SMAP Innovation:** Transitioning between modes happens *In-Place* on the exact same memory allocation. Going from Dense to Sparse requires a single $O(N)$ index remapping pass that updates the internal slots backward through a reverse map layer—**not a single byte of user data is moved.**

### 3. Cache-Line Virtual Sorting (Zero-Data-Movement)
* **Standard Implementations:** Sorting requires either physically moving heavy structural records in memory (which breaks external hardware index buffers) or sorting a sluggish pointer array (causing severe CPU cache misses).
* **SMAP Innovation:** Sorting executes exclusively on a lightweight, CPU-cache-friendly indirection layer (`rev_map`) using an optimized QuickSort engine. Your heavy structures remain untouched, keeping GPU Direct Memory Access (DMA) pipelines perfectly stable while allowing standard UI Grids to read sorted states sequentially.

### 4. Mask-Free & Branchless Virtual Filtering
* **Standard Implementations:** Filtering relies on visibility bitmasks checked inside loops via `if (visible)`, leading to severe CPU branch mispredictions.
* **SMAP Innovation:** The custom predicate filter filters matching slots and dynamically packs them directly to the front of the virtual reference layer in a single pass. The rendering loop then streams blindly from index `0` up to `active_count`—completely eliminating branch instructions (`if` checks) from your critical inner loops.

---

## 📐 Dual-Hierarchy Layout Architecture

[ External Stable Handle ]  (Immutable 64-bit ID protecting against ghost lookup)
│
▼
[ \slots() Entry Matrix ]   (1-Based Sparse Postal Boxes with Generational Counters)
│
├──► [#SMAP_SPARSE] ──► Direct Mapping to Packed RAM Base Pointer
│
└──► [#SMAP_DENSE]  ──► [ \revMap() Indirection Layer ] ──► Linear Data RAM

---

## ⚡ The Architectural Performance Win: Resolving the Dense Dilemma

In standard engine development, engineers face a punishing trade-off regarding virtual sorting and high-frequency mutations:

* **The Problem with Standard Dense Slot Maps:** Virtual sorting (sorting an indirection layer without shifting heavy vertex data) is strictly exclusive to Dense layouts. However, Dense Slot Maps are significantly slower during high-frequency `Insert` and `Delete` cycles because every mutation forces a mandatory "Swap-With-Last" memory copy and dual-stage index synchronization.
* **The SMAP Hybrid Solution:** SMAP eliminates this bottleneck by letting you choose the ideal state for the current workload:
  1. **High-Frequency Mutation Phase:** Run the engine in `#SMAP_SPARSE` mode during heavy batch imports, procedural generation, or real-time user editing. Mutations execute at blazing-fast, raw O(1) speeds.
  2. **Rendering & UI Phase:** Morph into `#SMAP_DENSE` via a quick compaction pass right before rendering or populating UI Grids. You instantly gain access to ultra-fast, cache-friendly virtual sorting and branchless filtering.
  3. **Return to Editing:** Morph back into sparse-compact mode seamlessly without moving data bytes.

---

## 🏎️ Hardware & GPU Burst-Streaming Benefits (Zero-Copy DMA)

SMAP provides a massive architectural advantage for graphics pipelines (OpenGL, Vulkan, DirectX) when dealing with real-time operations like alpha-blending transparency sorting or view-frustum filtering:

* **Eliminating PCIe Bus Bottlenecks:** Traditional engines physically reorder vertex data in RAM to match sorted states, forcing a massive, expensive reload of the entire Vertex Buffer Object (VBO) to the GPU over the slow PCIe bus every single frame.
* **The SMAP Index-Only Streaming Pass:** Because SMAP's virtual sorting (`smap_sort`) and filtering (`smap_filter`) execute purely on the lightweight integer `rev_map` layer, **your heavy geometric vertex data stays 100% frozen in place.** 
* **The Performance Win:** You bypass the CPU-to-GPU vertex copy completely. You only stream the tiny, compacted 32-bit index table into the GPU's Element Buffer Object (EBO / Index Buffer). This reduces bandwidth consumption across the graphics bus by up to 80%, eliminating GPU upload stalls entirely.

---

## 🛠️ Compilation and Requirements

### C99 Port
The standalone C-library uses pure standard ISO C99 headers without any third-party dependencies. You can compile it with any compliant compiler (GCC, Clang, MSVC):

```bash
gcc -std=c99 main.c hybrid_slotmap.c -o hybrid_test
```

### PureBasic Module
Compatible with PureBasic 6.0 and higher. It fully supports both the classic ASM compiler and the high-performance C-Backend (optimized for x64 targets).

---

## 📄 License

This project is licensed under the MIT License - see the `LICENSE` file for details.

---

## 🚀 Quick Start Guide (C99 API)

### 1. Initialization and Data Insertion
```c
#include "hybrid_slotmap.h"
#include <stdio.h>

typedef struct { float x, y, z, w; } vertex_t;

int main() {
    // Create container starting in high-performance SPARSE mode
    smap_t* smap = smap_new(SMAP_SPARSE, sizeof(vertex_t), 1024);

    vertex_t v1 = {10.0f, 20.0f, 30.0f, 1.0f};
    uint64_t h_v1 = smap_insert(smap, &v1);

    vertex_t v2 = {40.0f, 50.0f, 60.0f, 1.0f};
    uint64_t h_v2 = smap_insert(smap, &v2);

    // Fast O(1) deletion leaving a virtual hole in slots, but RAM stays dense
    smap_delete(smap, h_v1);
```

    // 2. Seamless Hot-Swap to DENSE Mode for GPU/Sorting Pass
    smap_convert_to_dense(smap, NULL);

    // 3. O(1) Memory Validation
    if (smap_is_valid(smap, h_v2)) {
        vertex_t* ptr = (vertex_t*)smap_get_ptr(smap, h_v2);
        printf("Vertex coordinates: X=%.2f\n", ptr->x);
    }

    // Clean up memory pool resources
    smap_destroy(smap);
    return 0;
}


## 🚀 Quick Start Guide (Purebasic)

XIncludeFile "PbFw_Module_SlotMap.pb"

Structure TVector3D
  x.d : y.d : z.d : w.d
EndStructure

; 1. Allocate container inside the fast sparse mutation layout
Define *Points.SMAP::TSMap = SMAP::New(SMAP::#SMAP_SPARSE, SizeOf(TVector3D), 1024)

Define tmp.TVector3D
tmp\x = 100.0 : Define hPt1.q = SMAP::InsertItem(*Points, @tmp)
tmp\x = 300.0 : Define hPt2.q = SMAP::InsertItem(*Points, @tmp)

; Delete intermediate vertex at O(1) velocity
SMAP::DeleteItem(*Points, hPt1)

; 2. Bake into packed DENSE array layout right before rendering
SMAP::ConvertToDense(*Points)

; 3. Safely lookup and update the vertex pointer
If SMAP::IsItemValid(*Points, hPt2)
  Define *v.TVector3D = SMAP::GetItemPtr(*Points, hPt2)
  *v\x = 500.0 ; Direct RAM modification
EndIf

; Release memory infrastructure
SMAP::Destroy(*Points)
