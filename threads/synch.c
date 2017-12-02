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


bool semaphore_priority_comparator (struct list_elem *first, struct list_elem *second, void *aux);
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
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_insert_ordered (&sema->waiters, &thread_current ()->elem, priority_comparator, NULL);
      thread_current ()->waiting_sema = sema;
      thread_block ();
      thread_current ()->waiting_sema = NULL;
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

  ASSERT (sema != NULL);
  struct thread *t = NULL;
  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    {
      t = list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem);
      thread_unblock (t);
    }
  sema->value++;
  intr_set_level (old_level);
  if (t != NULL)
    {
      if (t->priority > thread_get_priority ())
        thread_yield ();
    }
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Returns the priority of the highest-priority thread waiting
   on the semaphore sema */
int
get_semaphore_priority (struct semaphore *sema)
{
  ASSERT (sema != NULL);
  if (list_empty (&sema->waiters))
    return PRI_MIN;
  struct list_elem *front = list_front (&sema->waiters);
  struct thread *t = list_entry (front, struct thread, elem);
  return t->priority;
}

/* Returns the priority of the highest-priority thread waiting
   on the lock lock */
int
get_lock_priority (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock->holder != NULL);
  return get_semaphore_priority (&lock->semaphore);
}

/* Compares two locks and returns true if the priority of the first lock
   is greater than that of the second lock */
bool
lock_list_priority_comparator (struct list_elem *first, struct list_elem *second, void *aux)
{
  ASSERT (first != NULL);
  ASSERT (second != NULL);
  struct lock *first_lock = list_entry (first, struct lock, elem);
  struct lock *second_lock = list_entry (second, struct lock, elem);
  return get_lock_priority (first_lock) > get_lock_priority (second_lock);
}

/* Sorts the list of threads waiting on the semaphore sema */
void
sort_sema_waiters (struct semaphore *sema)
{
  if (sema == NULL)
    return;
  list_sort (&sema->waiters, priority_comparator, NULL);
}

/* Sorts the list of threads waiting on the conditional variable condvar */
void
sort_condvar_waiters (struct condition *condvar)
{
  if (condvar == NULL)
    return;
  list_sort (&condvar->waiters, semaphore_priority_comparator, NULL);
}

/* Called when the current thread calling lock_acquire has a higher priority
   than the current lock holder. Modified the priority of the lock holder to
   match that of the current thread and if the lock holder is waiting on another lock
   it donates its priority recursively to the holder of that other lock. */
void
donate_priority (struct lock *lock)
{
  if (lock == NULL)
    return;
  if (lock->holder != NULL)
    {
      if (thread_get_priority () > lock->holder->priority)
        {
          lock->holder->priority = thread_get_priority ();
          donate_priority (lock->holder->waiting_lock);
          if (lock->holder->waiting_lock != NULL)
            sort_sema_waiters (&lock->holder->waiting_lock->semaphore);
          if (lock->holder->waiting_sema != NULL)
            sort_sema_waiters (lock->holder->waiting_sema);
          if (lock->holder->waiting_condvar != NULL)
            sort_condvar_waiters (lock->holder->waiting_condvar);
        }
    }
  return;
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
  sema_init (&lock->semaphore, 1);
}

static void lock_acquire_ps (struct lock *lock);

static void lock_acquire_mlfqs (struct lock *lock);

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
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  thread_current ()->waiting_lock = lock;
  switch (scheduler)
    {
      case MLFQS:
        lock_acquire_mlfqs (lock);
        break;
      default:
        lock_acquire_ps (lock);
    }
  thread_current ()->waiting_lock = NULL;
}

/* Handles the priority donation with the Priority Scheduler(PS)
   as well as checking for the availability of the required lock 
   if it is available it acquires it else it waits for it 
   till it becomes available. */
static void
lock_acquire_ps (struct lock *lock)
{
  donate_priority (lock);
  priority_sort_ready_list ();
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  list_insert_ordered (&thread_current ()->acquired_locks, &lock->elem,
                       lock_list_priority_comparator, NULL);
}

/* Checks for the availability of the required lock if it is available 
   it acquires it else it waits for it till it becomes available with 
   the Multi-level Feedback Queue Scheduler(MLFQS). */
static void
lock_acquire_mlfqs (struct lock *lock)
{
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
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
    {
      lock->holder = thread_current ();
      list_insert_ordered (&thread_current ()->acquired_locks, &lock->elem,
                           lock_list_priority_comparator, NULL);
    }
  return success;
}

static void
lock_release_ps (struct lock *lock)
{
  list_remove (&lock->elem);
  if (list_empty (&thread_current ()->acquired_locks))
    thread_current ()->priority = thread_current ()->orig_priority;
  else
    {
      struct lock *donating_lock = list_entry (list_front (&thread_current ()->acquired_locks), struct lock, elem);
      if (get_lock_priority (donating_lock) > thread_current ()->orig_priority)
        thread_current ()->priority = get_lock_priority (donating_lock);
      else
        thread_current ()->priority = thread_current ()->orig_priority;
    }
  priority_sort_ready_list ();
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  lock->holder = NULL;
  switch (scheduler)
    {
      case MLFQS:
        break;
      default:
        lock_release_ps (lock);
    }
  sema_up (&lock->semaphore);
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

/* Compares two semaphores and returns true if the priority of the first semaphore
   is greater than that of the second semaphore */
bool
semaphore_priority_comparator (struct list_elem *first, struct list_elem *second, void *aux)
{
  struct semaphore_elem *first_sema = list_entry (first, struct semaphore_elem, elem);
  struct semaphore_elem *second_sema = list_entry (second, struct semaphore_elem, elem);
  return get_semaphore_priority (&first_sema->semaphore) > get_semaphore_priority (&second_sema->semaphore);
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
  
  sema_init (&waiter.semaphore, 0);
  
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  thread_current ()->waiting_condvar = cond; 
  sema_down (&waiter.semaphore);
  thread_current ()->waiting_condvar = NULL;
  lock_acquire (lock);
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
    {
      list_sort (&cond->waiters, semaphore_priority_comparator, NULL);
      sema_up (&list_entry (list_pop_front (&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
    }
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
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  list_sort (&cond->waiters, semaphore_priority_comparator, NULL);
  while (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
}
