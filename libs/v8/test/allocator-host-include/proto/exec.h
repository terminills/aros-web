#pragma once
#include <exec/types.h>
#include <pthread.h>
#include <assert.h>
struct SignalSemaphore { pthread_mutex_t lock; };
static inline void InitSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_init(&s->lock, 0)); }
static inline void ObtainSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_lock(&s->lock)); }
static inline void ReleaseSemaphore(struct SignalSemaphore *s) { assert(!pthread_mutex_unlock(&s->lock)); }
