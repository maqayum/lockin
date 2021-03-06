/*
 * File: ttas_rw_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef _TTAS_RW_IN_H_
#define _TTAS_RW_IN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <malloc.h>
#include <limits.h>

#if !defined(__x86_64__)
#  error This file is designed to work only on x86_64 architectures! 
#endif

/* ******************************************************************************** */
/* settings *********************************************************************** */
#if !defined(PADDING)
#  define PADDING         1        /* padd locks/conditionals to cache-line */
#endif
#define TTAS_RW_COOP      1	/* spin for TTAS_RW_MAX_SPINS before calling */
#define TTAS_RW_MAX_SPINS 1024	/* sched_yield() to yield the cpu to others */
#define FREQ_CPU_GHZ      2.8
#define REPLACE_MUTEX     1	/* ovewrite the pthread_[mutex|cond] functions */
/* ******************************************************************************** */


#define TTAS_RW_FREE    0
#define TTAS_RW_WLOCKED 0xFF000000

#define CACHE_LINE_SIZE 64
#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct ttas_rw_lock 
{
  union
  {
    struct
    {
      volatile uint16_t nr;
      volatile uint8_t padding;
      volatile uint8_t w;
    } rw;
    volatile uint32_t val;
  } lock;

#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} ttas_rw_lock_t;

#define TTAS_RW_LOCK_INITIALIZER { .lock.val = TTAS_RW_FREE }

/* returns 0 on success */
static inline int
ttas_rw_lock_tryrlock(ttas_rw_lock_t* lock) 
{
  while (lock->lock.rw.w == TTAS_RW_FREE)
    {
      uint32_t lock_old = lock->lock.val & TTAS_RW_FREE;
      uint32_t lock_new = lock_old + 1;
      uint32_t cas = __sync_val_compare_and_swap(&lock->lock.val, lock_old, lock_new);
      if (cas == lock_old)
	{
	  return 0;
	}
      else if (cas & TTAS_RW_WLOCKED) /* we have a writer in the meanime! */
	{
	  break;
	}
    }
  return 1;
}

/* returns 0 on success */
static inline int
ttas_rw_lock_trywlock(ttas_rw_lock_t* lock) 
{
  if (lock->lock.val != TTAS_RW_FREE)
    {
      return 1;
    }
  return (__sync_val_compare_and_swap(&lock->lock.val, TTAS_RW_FREE, TTAS_RW_WLOCKED) != TTAS_RW_FREE);
}

static inline int
ttas_rw_lock_rlock(ttas_rw_lock_t* lock) 
{
  (int) __sync_add_and_fetch(&lock->lock.val, 1);
#if TTAS_COOP == 1
  size_t spins = 0;
  while (lock->lock.rw.w != TTAS_RW_FREE)
    {
      PAUSE_IN();
      if (spins++ == TTAS_MAX_SPINS << 2)
	{
	  sched_yield();
	  spins = 0;
	}
    }
#else
  while (lock->lock.rw.w != TTAS_RW_FREE)
    {
      PAUSE_IN();
    }
#endif
  return 0;
}

static inline int
ttas_rw_lock_wlock(ttas_rw_lock_t* lock) 
{
#if TTAS_COOP == 1
  size_t spins = 0;
  do
    {
      while (lock->lock.val != TTAS_RW_FREE)
	{
	  PAUSE_IN();
	  if (spins++ == TTAS_MAX_SPINS)
	    {
	      sched_yield();
	      spins = 0;
	    }
	}
    }
  while (__sync_val_compare_and_swap(&lock->lock.val, TTAS_RW_FREE, TTAS_RW_WLOCKED) != TTAS_RW_FREE);
#else
  do
    {
      while (lock->lock.val != TTAS_RW_FREE)
	{
	  PAUSE_IN();
	}
    }
  while (__sync_val_compare_and_swap(&lock->lock.val, TTAS_RW_FREE, TTAS_RW_WLOCKED) != TTAS_RW_FREE);
#endif
  return 0;
}

static inline int
ttas_rw_lock_unlock(ttas_rw_lock_t* lock) 
{
  asm volatile("" ::: "memory");
  if (lock->lock.rw.w != TTAS_RW_FREE)
    {
      lock->lock.rw.w = TTAS_RW_FREE;
    }
  else if (lock->lock.rw.nr > 0)
    {
      (int) __sync_sub_and_fetch(&lock->lock.val, 1);
    }
  return 0;
}


static inline int
ttas_rw_lock_init(ttas_rw_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->lock.val = TTAS_RW_FREE;
  asm volatile ("mfence");
  return 0;
}

static inline int
ttas_rw_lock_destroy(ttas_rw_lock_t* the_lock) 
{
    return 0;
}


#if REPLACE_MUTEX == 1
#  define pthread_rwlock_init    ttas_rw_lock_init
#  define pthread_rwlock_destroy ttas_rw_lock_destroy
#  define pthread_rwlock_rdlock  ttas_rw_lock_rlock
#  define pthread_rwlock_wrlock  ttas_rw_lock_wlock
#  define pthread_rwlock_unlock  ttas_rw_lock_unlock
#  define pthread_rwlock_tryrdlock ttas_rw_lock_tryrlock
#  define pthread_rwlock_trywrlock ttas_rw_lock_trywlock
#  define pthread_rwlock_t       ttas_rw_lock_t
#  undef  PTHREAD_RWLOCK_INITIALIZER
#  define PTHREAD_RWLOCK_INITIALIZER TTAS_RW_LOCK_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


