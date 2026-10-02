#include "allocation.h"
#include <stdatomic.h>
#include <stddef.h>
#include "osapi/osapi_heap.h"

static atomic_bool monitor;
static atomic_uint_fast64_t libc_calls;
static atomic_uint_fast64_t osapi_calls;

void PGW_allocation_monitor(bool enabled)
{
    atomic_store(&libc_calls, 0);
    atomic_store(&osapi_calls, 0);
    atomic_store(&monitor, enabled);
}
uint64_t PGW_allocation_calls(void) { return atomic_load(&libc_calls); }
uint64_t PGW_osapi_allocation_calls(void) { return atomic_load(&osapi_calls); }

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
    if (atomic_load(&monitor)) atomic_fetch_add(&libc_calls, 1);
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&libc_calls, 1);
    return __real_calloc(count, size);
}
void *__wrap_realloc(void *ptr, size_t size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&libc_calls, 1);
    return __real_realloc(ptr, size);
}
void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&libc_calls, 1);
    return __real_aligned_alloc(alignment, size);
}
int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&libc_calls, 1);
    return __real_posix_memalign(out, alignment, size);
}
void *__wrap_OSAPI_Heap_allocate(RTI_SIZE_T count, RTI_SIZE_T size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&osapi_calls, 1);
    return __real_OSAPI_Heap_allocate(count, size);
}
void *__wrap_OSAPI_Heap_realloc(void *ptr, RTI_SIZE_T size)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&osapi_calls, 1);
    return __real_OSAPI_Heap_realloc(ptr, size);
}
void __wrap_OSAPI_Heap_allocate_buffer(char **out, RTI_SIZE_T size, OSAPI_Alignment_T alignment)
{
    if (atomic_load(&monitor)) atomic_fetch_add(&osapi_calls, 1);
    __real_OSAPI_Heap_allocate_buffer(out, size, alignment);
}
