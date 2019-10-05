/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      struct thread *current_thread = thread_current();
      list_insert_ordered(&sema->waiters, &current_thread->elem, compare_priority, NULL);
      current_thread->affiliated_list = &sema->waiters;

      thread_block ();
    }

  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  struct thread * waken_up_thread = NULL;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
  {
    waken_up_thread = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    thread_unblock (waken_up_thread);
  }
  sema->value++;

  thread_current()->affiliated_list = NULL;

  /* If waken_up_thread has higher priority than that of 
   * the current thread, yield */
  if((waken_up_thread != NULL) && (waken_up_thread->priority >= thread_current()->priority))
    thread_yield();

  intr_set_level (old_level);
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */

void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  lock->donated_priority = -1;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  struct thread *current_thread;
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  
  current_thread = thread_current();
  
  if(thread_mlfqs == false)
  {
    /* The lock is acquired by another thread */
    if(lock->holder != NULL)
    {
      current_thread->pending_lock = lock;
      donate_priority(lock, current_thread->priority);
    }
  }
  
  sema_down (&lock->semaphore);
  
  /* ---------- */
  
  lock->holder = current_thread;
  /* Initialize donated_priority to the priority of the current thread */
  lock->donated_priority = current_thread->priority;
  
  current_thread->pending_lock = NULL;
  list_push_front(&current_thread->lock_list, &lock->elem);
}

void donate_priority(struct lock *lock, int priority)
{
  ASSERT (lock != NULL);
  ASSERT (thread_mlfqs == false);
  /* Only donates when the lock holder's priority is smaller */
  if(lock->holder->priority >= priority)
    return;

  /* Adjusts lock holder's priority and propagates */
  lock->donated_priority = priority;
  lock->holder->priority = priority;

  list_remove(&lock->holder->elem);
  list_insert_ordered(lock->holder->affiliated_list, &lock->holder->elem, compare_priority, NULL);
  /* list_sort(lock->holder->affiliated_list, compare_priority, NULL); */

  if(lock->holder->pending_lock)
    donate_priority(lock->holder->pending_lock, priority);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  struct thread *current_thread;
  
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  current_thread = thread_current();

  /* remove lock from lock_list of the current thread */
  list_remove(&lock->elem);
  lock->holder = NULL;
  sema_up (&lock->semaphore);
  
  if(thread_mlfqs == false)
  {
    adjust_priority();
    if(list_empty(&ready_list))
      return;
    else
      thread_yield();
  }
}


void
adjust_priority(void)
{
  struct thread *thread;
  struct list *lock_list;
  struct list_elem *e;
  int max_donated_priority = -1;

  ASSERT(thread_mlfqs == false);

  thread = thread_current();
  lock_list = &thread->lock_list;

  if(list_empty(lock_list))
  {
    /* If the lock_list is empty, set thread priority to original priority saved before lock acquisition */
    ASSERT (thread->original_priority != -1);
    thread->priority = thread->original_priority;
    return;
  }

  /* If the lock_list is nonempty, set thread priority to the maximum of donated priorities */
  for(e = list_begin(lock_list); e != list_end(lock_list); e = list_next(e))
  {
    struct lock *lock = list_entry(e, struct lock, elem);
    max_donated_priority = (lock->donated_priority > max_donated_priority) ? lock->donated_priority :
    max_donated_priority;
  }

  thread->priority = max_donated_priority;
  return;
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int priority;                       /* Priority of the thread waiting on this semaphore */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  /* Sets up the waiter */
  sema_init (&waiter.semaphore, 0);
  /* Note that only one thread will be waiting on the waiter.semaphore,
   * so it is logical that the priority of the waiter 
   * will be that of current thread. */
  waiter.priority = thread_current()->priority;
  list_insert_ordered(&cond->waiters, &waiter.elem, compare_priority_sema, NULL);
  /* list_push_back (&cond->waiters, &waiter.elem); */
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

bool
compare_priority_sema(const struct list_elem *a, const struct list_elem *b, void *aux)
{
  struct semaphore_elem *s_a = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *s_b = list_entry(b, struct semaphore_elem, elem);
  return !((s_a->priority)<=(s_b->priority));
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
