#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

static void halt (void);
static void exit (int);
static pid_t exec (const char *);
static int wait (pid_t);
static bool create (const char *, unsigned);
static bool remove (const char *);
static int open (const char *);
static int filesize (int);
static int read (int, void *, unsigned);
static int write (int, const void *, unsigned);
static void seek (int, unsigned);
static unsigned tell (int);
static void close (int);

void NO_RETURN
abort (void)
{
  exit (-1);
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Checks if ptr is a valid user space
   pointer. */
static bool
is_valid_userspace_ptr (const void *ptr)
{
  return !(ptr >= PHYS_BASE || get_user (ptr) == -1);
}

/* Checks if the given string is a valid
   userspace string. */
static bool
is_valid_userspace_string (const char *ptr)
{
  if (!is_valid_userspace_ptr (ptr))
    return false;
  for (; get_user ((uint8_t *) ptr) != '\0' ; ++ptr)
    {
      if (ptr >= (char *) PHYS_BASE || get_user ((uint8_t *) ptr) == -1)//get_user (ptr) == -1)
        return false;
    }
  return true;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

bool
check_stack (int syscall_num, uint32_t *esp)
{
  switch (syscall_num)
    {
      case SYS_HALT:
        return true;
      case SYS_EXIT:
      case SYS_EXEC:
      case SYS_WAIT:
      case SYS_REMOVE:
      case SYS_OPEN:
      case SYS_FILESIZE:
      case SYS_TELL:
      case SYS_CLOSE:
        return is_valid_userspace_ptr (esp);
      case SYS_CREATE:
      case SYS_SEEK:
        return is_valid_userspace_ptr (esp) && is_valid_userspace_ptr (esp + 1);
      case SYS_READ:
      case SYS_WRITE:
        return is_valid_userspace_ptr (esp)
        && is_valid_userspace_ptr (esp + 1)
        && is_valid_userspace_ptr (esp + 2);
      default:
        return false;
    }
}

/* Gets the syscall number from the stack
   and calls the appropriate handler function. */
static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = f->esp;
  if (!is_valid_userspace_ptr (esp))
    abort ();
            
  int syscall_num = *esp;
  if (!check_stack (syscall_num, esp + 1))
    abort ();
  switch (syscall_num)
    {
      case SYS_HALT:
        halt ();
        break;
      case SYS_EXIT:
        exit (*(esp + 1));
        break;
      case SYS_EXEC:
        f->eax = exec ((char *) *(esp + 1));
        break;
      case SYS_WAIT:
        f->eax = wait ((pid_t) *(esp + 1));
        break;
      case SYS_CREATE:
        f->eax = create ((char *) *(esp + 1), *(esp + 2));
        break;
      case SYS_REMOVE:
        f->eax = remove ((char *) *(esp + 1));
        break;
      case SYS_OPEN:
        f->eax = open ((char *) *(esp + 1));
        break;
      case SYS_FILESIZE:
        f->eax = filesize (*(esp + 1));
        break;
      case SYS_READ:
        f->eax = read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_WRITE:
        f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_SEEK:
        seek (*(esp + 1), *(esp + 2));
        break;
      case SYS_TELL:
        f->eax = tell (*(esp + 1));
        break;
      case SYS_CLOSE:
        close (*(esp + 1));
        break;
      default:
        break;
    }
}

/* Implements the halt system call. */
static void
halt (void)
{
  shutdown_power_off ();
}

/* Implements the exit system call. */
static void NO_RETURN
exit (int status)
{
  pid_t parent_pid = thread_current ()->ppid;
  struct thread *parent_thread = get_thread_by_id (parent_pid);
  if (parent_thread != NULL)
    {
      lock_acquire (&parent_thread->wait_lock);
      struct child_info *child = get_child_info_by_id (&parent_thread->child_processes, thread_current ()->tid);
      child->exit_status = status;
      child->is_exited = true;
      lock_release (&parent_thread->wait_lock);
    }
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

/* Implements the exec system call. */
static pid_t
exec (const char *cmd_line)
{
  if (!is_valid_userspace_string (cmd_line))
    {
      abort ();
      return -1;
    }
  lock_acquire (&thread_current ()->exec_lock);
  pid_t child_pid = process_execute (cmd_line);
  cond_wait (&thread_current ()->exec_condvar, &thread_current ()->exec_lock); 
  if (!thread_current ()->child_loaded_successfully)
    {
      lock_release (&thread_current ()->exec_lock);
      return -1;
    }
  lock_release (&thread_current ()->exec_lock);
  return child_pid;
}

/* Implements the wait system call. */
static int
wait (pid_t pid)
{
  return process_wait (pid);
}

/* Allocates a new file descriptor. */
static int
allocate_fd (void) 
{
  return thread_current ()->next_fd++;
}

/* Gets the struct open_file pointed to by fd. */
static struct open_file
*get_open_file (int fd)
{
  struct list_elem *cur = list_begin (&thread_current ()->open_files);
  for (; cur != list_end (&thread_current ()->open_files); cur = list_next (cur))
    {
      struct open_file *file_data = list_entry (cur, struct open_file, elem);
      if (file_data->fd == fd)
        return file_data; 
    }
  return NULL;
}

/* Gets the struct file pointed to by fd. */
static struct file
*get_file (int fd)
{
  struct open_file *file_data = get_open_file (fd);
  if (file_data == NULL)
    return NULL;
  return file_data->file;
}

/* Implements the create system call. */
static bool
create (const char *file, unsigned initial_size)
{
  if (!is_valid_userspace_string (file))
    {
      abort ();
      return false;
    }
  
  int len = strlen (file);
  if (len == 0 || len > FILE_NAME_MAX)
    return false;
  bool success;
  lock_acquire (&filesys_lock);
  success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

/* Implements the remove system call. */
static bool
remove (const char *file)
{
  if (!is_valid_userspace_string (file))
    {
      abort ();
      return false;
    }
  bool success;
  lock_acquire (&filesys_lock);
  success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

/* Implements the open system call. */
static int
open (const char *file)
{
  struct open_file *file_data = malloc (sizeof(struct open_file));
  if (file_data == NULL)
    return -1;
  file_data->fd = allocate_fd ();
  if (!is_valid_userspace_string (file))
    {
      abort ();
      free (file_data);
      return -1;
    }

  lock_acquire (&filesys_lock);
  file_data->file = filesys_open (file);
  lock_release (&filesys_lock);

  if (file_data->file == NULL)
    return -1;
  list_push_back (&thread_current ()->open_files, &file_data->elem);
  return file_data->fd;
}

/* Implements the filesize system call. */
static int
filesize (int fd)
{
  struct file* file = get_file (fd);
  if (file == NULL)
    return -1;
  return file_length (file);
}

/* Implements the read system call. */
static int
read (int fd, void *buffer, unsigned size)
{
  if (!is_valid_userspace_ptr (buffer)
      || !is_valid_userspace_ptr (buffer + size - 1)
      || fd == STDOUT_FILENO)
    {
      /* terminate process */
      abort ();
      return -1;  
    }
  if (fd == STDIN_FILENO)
    {
      for (unsigned i = 0 ; i < size ; i++)
        *(char *)(buffer + i) = input_getc ();
      return size;
    }
  else
    {
      struct file* file = get_file (fd);
      if (file == NULL)
        return -1;
      int count;
      lock_acquire (&filesys_lock);
      count = file_read (file, buffer, size);
      lock_release (&filesys_lock);
      return count;
    }
}

/* Implements the write system call. */
static int
write (int fd, const void *buffer, unsigned size)
{
  if (!is_valid_userspace_ptr (buffer)
      || !is_valid_userspace_ptr (buffer + size - 1)
      || fd == STDIN_FILENO)
    {
      /* terminate process */
      abort ();
      return -1;
    }
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    {
      struct file* file = get_file (fd);
      if (file == NULL)
        return -1;
      int count;
      lock_acquire (&filesys_lock);
      count = file_write (file, buffer, size);
      lock_release (&filesys_lock);
      return count;
    }
}

/* Implements the seek system call. */
static void
seek (int fd, unsigned position)
{
  struct file* file = get_file (fd);
  if (file == NULL)
    return;
  file_seek (file, position);
}

static unsigned
tell (int fd)
{
  struct file* file = get_file (fd);
  if (file == NULL)
    return 0;
  return file_tell (file);
}

/* Implements the close system call. */
static void
close (int fd)
{
  struct open_file* file_data = get_open_file (fd);
  if (file_data == NULL || file_data->file == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_close (file_data->file);
  lock_release (&filesys_lock);
  list_remove (&file_data->elem);
  free (file_data);
}
