/*
 * Copyright (c) 2010 Tomas Martinec
 * All rights reserved.
 *
 * Distributed under the terms of GPL.
 *
 */

#ifndef BREAKPOINT_H_
#define BREAKPOINT_H_

#include <stdbool.h>

#include "../list.h"
#include "../mtypes.h"

/** Kind of code breakpoints */
typedef enum {
	BREAKPOINT_KIND_DEBUGGER  = 0x01,
	BREAKPOINT_KIND_SIMULATOR = 0x02
} breakpoint_kind_t;

/** Filter for filtering the code breakpoints according to the kind */
typedef enum {
	BREAKPOINT_FILTER_DEBUGGER  = BREAKPOINT_KIND_DEBUGGER,
	BREAKPOINT_FILTER_SIMULATOR = BREAKPOINT_KIND_SIMULATOR,
	BREAKPOINT_FILTER_ANY       = BREAKPOINT_KIND_DEBUGGER | BREAKPOINT_KIND_SIMULATOR
} breakpoint_filter_t;

/** Memory access types */
typedef enum {
	ACCESS_READ  = 0x01,
	ACCESS_WRITE = 0x02
} access_t;

/** Filter for filtering the memory operations according to the access type */
typedef enum {
	ACCESS_FILTER_NONE  = 0,
	ACCESS_FILTER_READ  = ACCESS_READ,
	ACCESS_FILTER_WRITE = ACCESS_WRITE,
	ACCESS_FILTER_ANY   = ACCESS_READ | ACCESS_WRITE
} access_filter_t;

/** Structure for the code breakpoints */
typedef struct {
	item_t item;
	
	breakpoint_kind_t kind;
	ptr_t pc;
	uint64_t hits;
} breakpoint_t;

/** Structure for the memory breakpoints */
typedef struct {
	item_t item;
	
	breakpoint_kind_t kind;
	ptr_t addr;
	uint64_t hits;
	access_filter_t access_flags;
} mem_breakpoint_t;

/* Memory breakpoints interface */

extern void memory_breakpoint_init_framework(void);
extern void memory_breakpoint_add(ptr_t address, breakpoint_kind_t kind,
    access_filter_t access_flags);
extern bool memory_breakpoint_remove(ptr_t address);
extern void memory_breakpoint_remove_filtered(breakpoint_filter_t filter);
extern void memory_breakpoint_check_for_breakpoint(ptr_t address,
    access_t access_type);
extern void memory_breakpoint_print_list(void);

/* Code breakpoints interface */

extern breakpoint_t *breakpoint_init(ptr_t address, breakpoint_kind_t kind);

extern breakpoint_t *breakpoint_find_by_address(list_t breakpoints,
    ptr_t address, breakpoint_filter_t filter);

extern bool breakpoint_check_for_code_breakpoints(void);

#endif