/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Builds Chromium's SQLite amalgamation for the probe tools the way
 * third_party/sqlite/sqlite3_shim_fixups.h does on AROS: exec/types.h
 * defines GLOBAL as a linkage qualifier, which collides with SQLite's
 * GLOBAL(t, v) accessor once pthread.h is pulled in.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <pthread.h>
#undef GLOBAL
extern char *getcwd(char *buffer, size_t size);
#include "sqlite3.c"
