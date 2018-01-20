#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Aborts the current running process with status -1. */
void abort (void);
/* Inits the syscall handler. */
void syscall_init (void);

#endif /* userprog/syscall.h */
