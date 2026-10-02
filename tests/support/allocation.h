#ifndef PGW_TEST_ALLOCATION_H
#define PGW_TEST_ALLOCATION_H
#include <stdbool.h>
#include <stdint.h>
void PGW_allocation_monitor(bool);
uint64_t PGW_allocation_calls(void);
uint64_t PGW_osapi_allocation_calls(void);
#endif
