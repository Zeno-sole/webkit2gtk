/*
 * Copyright (c) 2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef JIT_HEAP_CONFIG_H
#define JIT_HEAP_CONFIG_H

#include "pas_config.h"

#if PAS_ENABLE_JIT

#include "jit_heap_config_root_data.h"
#include "pas_bitfit_max_free.h"
#include "pas_heap_config_utils.h"

PAS_BEGIN_EXTERN_C;

#define JIT_SMALL_MIN_ALIGN_SHIFT 2u
#define JIT_SMALL_MIN_ALIGN (1u << JIT_SMALL_MIN_ALIGN_SHIFT)
#define JIT_SMALL_PAGE_SIZE 16384u
#define JIT_SMALL_GRANULE_SIZE 16384u
#define JIT_MEDIUM_MIN_ALIGN_SHIFT 8u
#define JIT_MEDIUM_MIN_ALIGN (1u << JIT_MEDIUM_MIN_ALIGN_SHIFT)
#define JIT_MEDIUM_PAGE_SIZE 131072u
#if PAS_ARM64
#define JIT_MEDIUM_GRANULE_SIZE 16384u
#else
#define JIT_MEDIUM_GRANULE_SIZE 4096u
#endif

PAS_API void jit_heap_config_activate(void);

static PAS_ALWAYS_INLINE size_t jit_type_size(pas_heap_type* type)
{
    PAS_TESTING_ASSERT(!type);
    return 1;
}
static PAS_ALWAYS_INLINE size_t jit_type_alignment(pas_heap_type* type)
{
    PAS_TESTING_ASSERT(!type);
    return 1;
}

PAS_API pas_page_base* jit_page_header_for_boundary_remote(pas_enumerator* enumerator, void* boundary);

static PAS_ALWAYS_INLINE pas_page_base* jit_small_bitfit_page_header_for_boundary(void* boundary);
static PAS_ALWAYS_INLINE void* jit_small_bitfit_boundary_for_page_header(pas_page_base* page);
PAS_API void* jit_small_bitfit_allocate_page(pas_segregated_heap* heap);
PAS_API pas_page_base* jit_small_bitfit_create_page_header(
    void* boundary, pas_lock_hold_mode heap_lock_hold_mode);
PAS_API void jit_small_bitfit_destroy_page_header(
    pas_page_base* page, pas_lock_hold_mode heap_lock_hold_mode);

PAS_BITFIT_PAGE_CONFIG_SPECIALIZATION_DECLARATIONS(jit_small_bitfit_page_config);

static PAS_ALWAYS_INLINE pas_page_base* jit_medium_bitfit_page_header_for_boundary(void* boundary);
static PAS_ALWAYS_INLINE void* jit_medium_bitfit_boundary_for_page_headery(pas_page_base* page);
PAS_API void* jit_medium_bitfit_allocate_page(pas_segregated_heap* heap);
PAS_API pas_page_base* jit_medium_bitfit_create_page_header(
    void* boundary, pas_lock_hold_mode heap_lock_hold_mode);
PAS_API void jit_medium_bitfit_destroy_page_header(
    pas_page_base* page, pas_lock_hold_mode heap_lock_hold_mode);

PAS_BITFIT_PAGE_CONFIG_SPECIALIZATION_DECLARATIONS(jit_medium_bitfit_page_config);

static PAS_ALWAYS_INLINE pas_fast_megapage_kind
jit_heap_config_fast_megapage_kind(uintptr_t begin)
{
    PAS_UNUSED_PARAM(begin);
    return pas_not_a_fast_megapage_kind;
}

static PAS_ALWAYS_INLINE pas_page_base* jit_heap_config_page_header(uintptr_t begin);
PAS_API pas_aligned_allocation_result jit_aligned_allocator(
    size_t size, pas_alignment alignment, pas_large_heap* large_heap, pas_heap_config* config);
PAS_API void* jit_prepare_to_enumerate(pas_enumerator* enumerator);
PAS_API bool jit_heap_config_for_each_shared_page_directory(
    pas_segregated_heap* heap,
    bool (*callback)(pas_segregated_shared_page_directory* directory,
                     void* arg),
    void* arg);

PAS_API bool jit_heap_config_for_each_shared_page_directory_remote(
    pas_enumerator* enumerator,
    pas_segregated_heap* heap,
    bool (*callback)(pas_enumerator* enumerator,
                     pas_segregated_shared_page_directory* directory,
                     void* arg),
    void* arg);

PAS_HEAP_CONFIG_SPECIALIZATION_DECLARATIONS(jit_heap_config);

#define JIT_BITFIT_PAGE_CONFIG(variant_lowercase, variant_uppercase) { \
        .base = { \
            .is_enabled = true, \
            .heap_config_ptr = &jit_heap_config, \
            .page_config_ptr = &jit_heap_config.variant_lowercase ## _bitfit_config.base, \
            .page_kind = pas_ ## variant_lowercase ## _bitfit_page_kind, \
            .min_align_shift = JIT_ ## variant_uppercase ## _MIN_ALIGN_SHIFT, \
            .page_size = JIT_ ## variant_uppercase ## _PAGE_SIZE, \
            .granule_size = JIT_ ## variant_uppercase ## _GRANULE_SIZE, \
            .page_header_size = PAS_BITFIT_PAGE_HEADER_SIZE( \
                JIT_ ## variant_uppercase ## _PAGE_SIZE, \
                JIT_ ## variant_uppercase ## _GRANULE_SIZE, \
                JIT_ ## variant_uppercase ## _MIN_ALIGN_SHIFT), \
            .page_header_for_boundary = jit_ ## variant_lowercase ## _bitfit_page_header_for_boundary, \
            .boundary_for_page_header = jit_ ## variant_lowercase ## _bitfit_boundary_for_page_header, \
            .page_header_for_boundary_remote = jit_page_header_for_boundary_remote, \
            .page_object_payload_offset = 0, \
            .page_object_payload_size = JIT_ ## variant_uppercase ## _PAGE_SIZE, \
            .max_object_size = \
                PAS_BITFIT_MAX_FREE_MAX_VALID << JIT_ ## variant_uppercase ## _MIN_ALIGN_SHIFT, \
            .page_allocator = jit_ ## variant_lowercase ## _bitfit_allocate_page, \
            .create_page_header = jit_ ## variant_lowercase ## _bitfit_create_page_header, \
            .destroy_page_header = jit_ ## variant_lowercase ## _bitfit_destroy_page_header \
        }, \
        .variant = pas_ ## variant_lowercase ## _bitfit_page_config_variant, \
        .kind = pas_bitfit_page_config_kind_jit_ ## variant_lowercase ## _bitfit, \
        PAS_BITFIT_PAGE_CONFIG_SPECIALIZATIONS(jit_ ## variant_lowercase ## _bitfit_page_config) \
    }

#define JIT_HEAP_CONFIG ((pas_heap_config){ \
        .config_ptr = &jit_heap_config, \
        .kind = pas_heap_config_kind_jit, \
        .activate_callback = jit_heap_config_activate, \
        .get_type_size = jit_type_size, \
        .get_type_alignment = jit_type_alignment, \
        .large_alignment = JIT_SMALL_MIN_ALIGN, \
        .small_segregated_config = { \
            .base = { \
                .is_enabled = false \
            } \
        }, \
        .medium_segregated_config = { \
            .base = { \
                .is_enabled = false \
            } \
        }, \
        .small_bitfit_config = JIT_BITFIT_PAGE_CONFIG(small, SMALL), \
        .medium_bitfit_config = JIT_BITFIT_PAGE_CONFIG(medium, MEDIUM), \
        .marge_bitfit_config = { \
            .base = { \
                .is_enabled = false \
            } \
        }, \
        .small_lookup_size_upper_bound = PAS_SMALL_LOOKUP_SIZE_UPPER_BOUND, \
        .fast_megapage_kind_func = jit_heap_config_fast_megapage_kind, \
        .small_segregated_is_in_megapage = false, \
        .small_bitfit_is_in_megapage = false, \
        .page_header_func = jit_heap_config_page_header, \
        .aligned_allocator = jit_aligned_allocator, \
        .aligned_allocator_talks_to_sharing_pool = true, \
        .deallocator = NULL, \
        .root_data = &jit_root_data, \
        .prepare_to_enumerate = jit_prepare_to_enumerate, \
        .for_each_shared_page_directory = jit_heap_config_for_each_shared_page_directory, \
        .for_each_shared_page_directory_remote = \
            jit_heap_config_for_each_shared_page_directory_remote, \
        PAS_HEAP_CONFIG_SPECIALIZATIONS(jit_heap_config) \
    })

PAS_API extern pas_heap_config jit_heap_config;

/* The JIT heap manages memory that is given to it by clients. Clients add memory to the JIT heap
   by freeing it into the fresh memory heap. The JIT heap never allocates pages from the OS by
   itself (though it could commit and decommit pages that you gave it). */
PAS_API extern pas_simple_large_free_heap jit_fresh_memory_heap;

PAS_API extern pas_large_heap_physical_page_sharing_cache jit_large_fresh_memory_heap;
PAS_API extern pas_page_header_table jit_small_bitfit_page_header_table;
PAS_API extern pas_page_header_table jit_medium_bitfit_page_header_table;
PAS_API extern pas_heap_runtime_config jit_heap_runtime_config;
PAS_API extern jit_heap_config_root_data jit_root_data;

static PAS_ALWAYS_INLINE pas_page_base* jit_small_bitfit_page_header_for_boundary(void* boundary)
{
    return pas_page_header_table_get_for_boundary(
        &jit_small_bitfit_page_header_table, JIT_SMALL_PAGE_SIZE, boundary);
}

static PAS_ALWAYS_INLINE void* jit_small_bitfit_boundary_for_page_header(pas_page_base* page)
{
    return pas_page_header_table_get_boundary(
        &jit_small_bitfit_page_header_table, JIT_SMALL_PAGE_SIZE, page);
}

static PAS_ALWAYS_INLINE pas_page_base* jit_medium_bitfit_page_header_for_boundary(void* boundary)
{
    return pas_page_header_table_get_for_boundary(
        &jit_medium_bitfit_page_header_table, JIT_MEDIUM_PAGE_SIZE, boundary);
}

static PAS_ALWAYS_INLINE void* jit_medium_bitfit_boundary_for_page_header(pas_page_base* page)
{
    return pas_page_header_table_get_boundary(
        &jit_medium_bitfit_page_header_table, JIT_MEDIUM_PAGE_SIZE, page);
}

static PAS_ALWAYS_INLINE pas_page_base* jit_heap_config_page_header(uintptr_t begin)
{
    pas_page_base* result;

    result = pas_page_header_table_get_for_address(
        &jit_small_bitfit_page_header_table, JIT_SMALL_PAGE_SIZE, (void*)begin);
    if (result)
        return result;

    return pas_page_header_table_get_for_address(
        &jit_medium_bitfit_page_header_table, JIT_MEDIUM_PAGE_SIZE, (void*)begin);
}

PAS_API void jit_heap_config_add_fresh_memory(pas_range range);

PAS_END_EXTERN_C;

#endif /* PAS_ENABLE_JIT */

#endif /* JIT_HEAP_CONFIG_H */

