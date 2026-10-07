/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the Software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#include "pgw/atomic.h"
#include "allocation.h"
#include <stddef.h>
#include "osapi/osapi_heap.h"

#ifndef PGW_ALLOCATION_INTERPOSITION_SUPPORTED
#define PGW_ALLOCATION_INTERPOSITION_SUPPORTED 0
#endif

static PGW_ATOMIC(RTI_UINT32) monitor;
static PGW_ATOMIC(RTI_UINT64) libc_calls;
static PGW_ATOMIC(RTI_UINT64) osapi_calls;

void PGW_allocation_monitor(bool enabled)
{
    PGW_ATOMIC_STORE_SEQ(&libc_calls, 0);
    PGW_ATOMIC_STORE_SEQ(&osapi_calls, 0);
    PGW_ATOMIC_STORE_SEQ(&monitor, enabled);
}
uint64_t PGW_allocation_calls(void) { return PGW_ATOMIC_LOAD_SEQ(&libc_calls); }
uint64_t PGW_osapi_allocation_calls(void) { return PGW_ATOMIC_LOAD_SEQ(&osapi_calls); }

#if PGW_ALLOCATION_INTERPOSITION_SUPPORTED
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t);
int __real_posix_memalign(void **, size_t, size_t);
void *__real_OSAPI_Heap_allocate(RTI_SIZE_T, RTI_SIZE_T);
void *__real_OSAPI_Heap_realloc(void *, RTI_SIZE_T);
void __real_OSAPI_Heap_allocate_buffer(char **, RTI_SIZE_T, OSAPI_Alignment_T);

void *__wrap_malloc(size_t size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&libc_calls, 1);
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&libc_calls, 1);
    return __real_calloc(count, size);
}
void *__wrap_realloc(void *ptr, size_t size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&libc_calls, 1);
    return __real_realloc(ptr, size);
}
void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&libc_calls, 1);
    return __real_aligned_alloc(alignment, size);
}
int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&libc_calls, 1);
    return __real_posix_memalign(out, alignment, size);
}
void *__wrap_OSAPI_Heap_allocate(RTI_SIZE_T count, RTI_SIZE_T size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&osapi_calls, 1);
    return __real_OSAPI_Heap_allocate(count, size);
}
void *__wrap_OSAPI_Heap_realloc(void *ptr, RTI_SIZE_T size)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&osapi_calls, 1);
    return __real_OSAPI_Heap_realloc(ptr, size);
}
void __wrap_OSAPI_Heap_allocate_buffer(char **out, RTI_SIZE_T size, OSAPI_Alignment_T alignment)
{
    if (PGW_ATOMIC_LOAD_SEQ(&monitor)) PGW_ATOMIC_ADD_SEQ(&osapi_calls, 1);
    __real_OSAPI_Heap_allocate_buffer(out, size, alignment);
}
#endif
