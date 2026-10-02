#include "pgw/diagnostics.h"

bool PGW_Counters_initialize(PGW_Counters *c)
{
    if (!c || sizeof(uint_fast64_t) != sizeof(uint64_t)) return false;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i) {
        atomic_init(&c->values[i], 0);
        if (!atomic_is_lock_free(&c->values[i])) return false;
    }
    return true;
}

bool PGW_Counters_add(PGW_Counters *c, PGW_CounterId id, uint64_t n)
{
    if (!c || (unsigned)id >= PGW_COUNT_TOTAL) return false;
    atomic_fetch_add_explicit(&c->values[id], n, memory_order_relaxed);
    return true;
}

bool PGW_Counters_snapshot(const PGW_Counters *c, uint32_t id,
                          uint64_t sequence, uint64_t time,
                          PGW_CounterSnapshot *out)
{
    if (!c || !out) return false;
    out->version = 1;
    out->entity_id = id;
    out->sequence = sequence;
    out->collected_ns = time;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i)
        out->values[i] = atomic_load_explicit(&c->values[i], memory_order_relaxed);
    return true;
}

bool PGW_Diagnostics_initialize(PGW_Diagnostics *d, const PGW_EventSeq *source)
{
    if (!d || !source) return false;
    RTI_INT32 capacity = PGW_EventSeq_get_maximum(source);
    RTI_INT32 length = PGW_EventSeq_get_length(source);
    PGW_Event *events = PGW_EventSeq_get_contiguous_buffer(source);
    if (capacity < 0 || length != 0 || (capacity && !events)) return false;
    d->initialized = false;
    if (!PGW_EventSeq_initialize(&d->events)) return false;
    if (capacity && !PGW_EventSeq_loan_contiguous(&d->events, events, 0, capacity)) {
        (void)PGW_EventSeq_finalize(&d->events);
        return false;
    }
    if (capacity && !PGW_EventSeq_set_length(&d->events, capacity)) {
        if (capacity) (void)PGW_EventSeq_unloan(&d->events);
        (void)PGW_EventSeq_finalize(&d->events);
        return false;
    }
    d->events_borrowed = capacity != 0;
    d->head = d->count = 0;
    d->minimum_severity = 2;
    atomic_flag_clear(&d->lock);
    atomic_init(&d->overflow, 0);
    atomic_init(&d->contention, 0);
    atomic_init(&d->drain_contention, 0);
    atomic_init(&d->rate_limited, 0);
    d->rate_interval_ns = d->rate_window_ns = 0;
    d->rate_max_events = d->rate_window_events = 0;
    if (atomic_is_lock_free(&d->overflow) && atomic_is_lock_free(&d->contention)) {
        d->initialized = true;
        return true;
    }
    if (d->events_borrowed) (void)PGW_EventSeq_unloan(&d->events);
    (void)PGW_EventSeq_finalize(&d->events);
    return false;
}

bool PGW_Diagnostics_finalize(PGW_Diagnostics *d)
{
    if (!d || !d->initialized) return false;
    if (atomic_flag_test_and_set_explicit(&d->lock, memory_order_acquire)) {
        atomic_fetch_add_explicit(&d->drain_contention, 1, memory_order_relaxed);
        return false;
    }
    bool result = !d->events_borrowed || PGW_EventSeq_unloan(&d->events);
    if (result) result = PGW_EventSeq_finalize(&d->events);
    d->head = d->count = 0;
    if (result) {
        d->initialized = false;
        d->events_borrowed = false;
    }
    atomic_flag_clear_explicit(&d->lock, memory_order_release);
    return result;
}

bool PGW_Diagnostics_configure_rate(PGW_Diagnostics *d, uint64_t interval, size_t maximum)
{
    if (!d || !d->initialized || ((interval == 0) != (maximum == 0))) return false;
    d->rate_interval_ns = interval;
    d->rate_max_events = maximum;
    d->rate_window_events = 0;
    d->rate_window_ns = 0;
    return true;
}

bool PGW_Diagnostics_snapshot(const PGW_Diagnostics *d, uint64_t time,
                              PGW_DiagnosticSnapshot *out)
{
    if (!d || !d->initialized || !out) return false;
    *out = (PGW_DiagnosticSnapshot){
        .version = 1, .collected_ns = time, .event_capacity = PGW_EventSeq_get_maximum(&d->events),
        .overflow = atomic_load_explicit(&d->overflow, memory_order_relaxed),
        .contention = atomic_load_explicit(&d->contention, memory_order_relaxed),
        .drain_contention = atomic_load_explicit(&d->drain_contention, memory_order_relaxed),
        .rate_limited = atomic_load_explicit(&d->rate_limited, memory_order_relaxed)
    };
    return true;
}

bool PGW_Diagnostics_emit(PGW_Diagnostics *d, const PGW_Event *e)
{
    if (!d || !d->initialized || !e) return false;
    if (e->severity < d->minimum_severity) return true;
    if (atomic_flag_test_and_set_explicit(&d->lock, memory_order_acquire)) {
        atomic_fetch_add_explicit(&d->contention, 1, memory_order_relaxed);
        return false;
    }
    if (d->rate_interval_ns) {
        if (e->time_ns >= d->rate_window_ns &&
            e->time_ns - d->rate_window_ns >= d->rate_interval_ns) {
            d->rate_window_ns = e->time_ns;
            d->rate_window_events = 0;
        }
        if (d->rate_window_events == d->rate_max_events) {
            atomic_fetch_add_explicit(&d->rate_limited, 1, memory_order_relaxed);
            atomic_flag_clear_explicit(&d->lock, memory_order_release);
            return false;
        }
        ++d->rate_window_events;
    }
    size_t capacity = PGW_EventSeq_get_length(&d->events);
    bool accepted = d->count < capacity;
    if (accepted) {
        *PGW_EventSeq_get_reference(&d->events, (d->head + d->count) % capacity) = *e;
        ++d->count;
    } else {
        atomic_fetch_add_explicit(&d->overflow, 1, memory_order_relaxed);
    }
    atomic_flag_clear_explicit(&d->lock, memory_order_release);
    return accepted;
}

bool PGW_Diagnostics_drain(PGW_Diagnostics *d, PGW_EventSeq *out)
{
    if (!d || !d->initialized || !out || out == &d->events) return false;
    size_t cap = PGW_EventSeq_get_maximum(out);
    uintptr_t destination = (uintptr_t)PGW_EventSeq_get_contiguous_buffer(out);
    uintptr_t source = (uintptr_t)PGW_EventSeq_get_contiguous_buffer(&d->events);
    size_t source_bytes = PGW_EventSeq_get_maximum(&d->events) * sizeof(PGW_Event);
    size_t destination_bytes = cap * sizeof(PGW_Event);
    if (source_bytes && destination_bytes && destination < source + source_bytes &&
        source < destination + destination_bytes) return false;
    if (cap && !PGW_EventSeq_set_length(out, 0)) return false;
    if (atomic_flag_test_and_set_explicit(&d->lock, memory_order_acquire)) {
        atomic_fetch_add_explicit(&d->drain_contention, 1, memory_order_relaxed);
        return false;
    }
    size_t n = d->count < cap ? d->count : cap;
    if (n && !PGW_EventSeq_set_length(out, n)) {
        atomic_flag_clear_explicit(&d->lock, memory_order_release);
        return false;
    }
    size_t capacity = PGW_EventSeq_get_length(&d->events);
    for (size_t i = 0; i < n; ++i)
        *PGW_EventSeq_get_reference(out, i) =
            *PGW_EventSeq_get_reference(&d->events, (RTI_INT32)((d->head + i) % capacity));
    if (capacity) d->head = (d->head + n) % capacity;
    d->count -= n;
    atomic_flag_clear_explicit(&d->lock, memory_order_release);
    return true;
}

typedef struct { char *data; size_t cap; size_t length; bool fits; } Buffer;

static void text(Buffer *b, const char *s)
{
    for (; *s; ++s) {
        if (b->length + 1 < b->cap) b->data[b->length] = *s;
        else b->fits = false;
        ++b->length;
    }
}

static void number(Buffer *b, uint64_t value)
{
    char digits[20];
    size_t n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) {
        char s[2] = {digits[--n], 0};
        text(b, s);
    }
}

bool PGW_snapshot_json(const PGW_CounterSnapshot *s, char *out,
                       size_t cap, size_t *length)
{
    if (!s || s->version != 1 || !out || !cap || !length) return false;
    Buffer b = {out, cap, 0, true};
    text(&b, "{\"version\":"); number(&b, s->version);
    text(&b, ",\"entity_id\":"); number(&b, s->entity_id);
    text(&b, ",\"sequence\":"); number(&b, s->sequence);
    text(&b, ",\"collected_ns\":"); number(&b, s->collected_ns);
    text(&b, ",\"counters\":[");
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i) {
        if (i) text(&b, ",");
        number(&b, s->values[i]);
    }
    text(&b, "]}\n");
    out[b.length < cap ? b.length : cap - 1] = 0;
    *length = b.length;
    return b.fits;
}
