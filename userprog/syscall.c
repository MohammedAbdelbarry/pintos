#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

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
 	

static struct lock filesys_lock;

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
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
is_valid_userspace_string (char *ptr)
{
  if (ptr == NULL || ptr >= PHYS_BASE)
    return false;
  for (; *ptr != '\0' ; ++ptr)
    {
      if (ptr >= PHYS_BASE || pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)//get_user (ptr) == -1)
        return false;
    }
  return true;
}

static bool
is_valid_userspace_ptr (void *ptr)
{
  return !(ptr == NULL || ptr >= PHYS_BASE || pagedir_get_page (thread_current ()->pagedir, ptr) == NULL);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = f->esp;
  int syscall_num = *esp;
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

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit (int status)
{
  pid_t parent_pid = thread_current ()->ppid;
  struct thread *parent_thread = get_thread_by_id (parent_pid);
  if (parent_thread != NULL)
    {
      struct child_info *child = get_child_info_by_id (&parent_thread->child_processes, thread_current ()->tid);
      child->exit_status = status;
    }
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

static pid_t
exec (const char *cmd_line)
{
  return 0;
}

static int
wait (pid_t pid)
{
  return process_wait (pid);
}

static int
allocate_fd (void) 
{
  int fd;

  lock_acquire (&thread_current ()->fd_lock);
  fd = thread_current ()->next_fd++;
  lock_release (&thread_current ()->fd_lock);

  return fd;
}

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

static struct file
*get_file (int fd)
{
  struct open_file *file_data = get_open_file (fd);
  if (file_data == NULL)
    return NULL;
  return file_data->file;
}

static bool
create (const char *file, unsigned initial_size)
{
  // printf ("File Create Ptr: %p\n", file);
  if (!is_valid_userspace_string (file))
    {
      // TODO: Abort the process here
      return false;
    }
  // printf ("Creating File: %s with size: %d\n", file, initial_size);
  int len = strlen (file);
  if (len == 0 || len > FILE_NAME_MAX)
    return false;
  bool success;
  lock_acquire (&filesys_lock);
  success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

static bool
remove (const char *file)
{
  if (!is_valid_userspace_string (file))
    {
      // TODO: Abort the process here
      return false;
    }
  // TODO: Check for process references on the file.
  bool success;
  lock_acquire (&filesys_lock);
  success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

static int
open (const char *file)
{
  struct open_file *file_data = malloc (sizeof(struct open_file));
  if (file_data == NULL)
    return -1;
  file_data->fd = allocate_fd ();
  // printf ("File Open Ptr: %p\n", file);
  if (!is_valid_userspace_string (file))
    {
      // TODO: Abort the process here
      free (file_data);
      return -1;
    }
  // printf ("Opening File: %s\n", file);

  lock_acquire (&filesys_lock);
  file_data->file = filesys_open (file);
  lock_release (&filesys_lock);
  
  if (file_data->file == NULL)
    return -1;
  list_push_back (&thread_current ()->open_files, &file_data->elem);
  return file_data->fd;
}

static int
filesize (int fd)
{
  struct file* file = get_file (fd);
  if (file == NULL)
    return -1;
  return file_length (file);
}

static int
read (int fd, void *buffer, unsigned size)
{
  if (!is_valid_userspace_ptr (buffer), !is_valid_userspace_ptr (buffer + size - 1))
    {
      /* terminate process */
      return -1;  
    }
  // printf ("read fd:%d\n", fd);
  if (fd == STDIN_FILENO)
    {
      for (int i = 0 ; i < size ; i++)
        *(char *)(buffer + i) = input_getc ();
      return size;
    }
  else
    {
      struct file* file = get_file (fd);
      // printf ("File: %p\n", file);
      // printf ("List Length: %d\n", list_size (&thread_current ()->open_files));
      if (file == NULL)
        return -1;
      int count;
      lock_acquire (&filesys_lock);
      count = file_read (file, buffer, size);
      lock_release (&filesys_lock);
      return count;
    }
}

static int
write (int fd, const void *buffer, unsigned size)
{
  if (!is_valid_userspace_ptr (buffer), !is_valid_userspace_ptr (buffer + size - 1))
    {
      /* terminate process */
      return -1;
    }
  // printf ("write fd:%d\n", fd);
  if (fd == STDOUT_FILENO)
    {
      //printf ("%d\t%p\t%d\n", fd, buffer, size);
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
