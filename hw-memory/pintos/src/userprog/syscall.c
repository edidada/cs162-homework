#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

void syscall_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}

/*
 * This does not check that the buffer consists of only mapped pages; it merely
 * checks the buffer exists entirely below PHYS_BASE.
 */
static void validate_buffer_in_user_region(const void* buffer, size_t length) {
  uintptr_t delta = PHYS_BASE - buffer;
  if (!is_user_vaddr(buffer) || length > delta)
    syscall_exit(-1);
}

/*
 * This does not check that the string consists of only mapped pages; it merely
 * checks the string exists entirely below PHYS_BASE.
 */
static void validate_string_in_user_region(const char* string) {
  uintptr_t delta = PHYS_BASE - (const void*)string;
  if (!is_user_vaddr(string) || strnlen(string, delta) == delta)
    syscall_exit(-1);
}

static int syscall_open(const char* filename) {
  struct thread* t = thread_current();
  if (t->open_file != NULL)
    return -1;

  t->open_file = filesys_open(filename);
  if (t->open_file == NULL)
    return -1;

  return 2;
}

static int syscall_write(int fd, void* buffer, unsigned size) {
  struct thread* t = thread_current();
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  } else if (fd != 2 || t->open_file == NULL)
    return -1;

  return (int)file_write(t->open_file, buffer, size);
}

static int syscall_read(int fd, void* buffer, unsigned size) {
  struct thread* t = thread_current();
  if (fd != 2 || t->open_file == NULL)
    return -1;

  return (int)file_read(t->open_file, buffer, size);
}

static void syscall_close(int fd) {
  struct thread* t = thread_current();
  if (fd == 2 && t->open_file != NULL) {
    file_close(t->open_file);
    t->open_file = NULL;
  }
}

/* Commit the byte break only after all newly required mappings succeed. */
static void *syscall_sbrk(intptr_t increment) {
  struct thread *t = thread_current();
  uintptr_t old = (uintptr_t)t->heap_break;
  uintptr_t bottom = (uintptr_t)t->heap_start;
  uintptr_t limit = (uintptr_t)PHYS_BASE;
  uintptr_t next;
  if (!bottom) return (void *)-1;
  if (increment >= 0) {
    if ((uintptr_t)increment >= limit - old) return (void *)-1;
    next = old + (uintptr_t)increment;
  } else {
    /* Unsigned subtraction handles INTPTR_MIN without signed negation. */
    uintptr_t amount = 0u - (uintptr_t)increment;
    if (amount > old - bottom) return (void *)-1;
    next = old - amount;
  }
  uintptr_t old_end = (old + PGMASK) & ~PGMASK;
  uintptr_t new_end = (next + PGMASK) & ~PGMASK;
  if (new_end > old_end) {
    uintptr_t page;
    for (page = old_end; page < new_end; page += PGSIZE) {
      if (pagedir_get_page(t->pagedir, (void *)page)) break;
      void *frame = palloc_get_page(PAL_USER | PAL_ZERO);
      if (!frame) break;
      if (!pagedir_set_page(t->pagedir, (void *)page, frame, true)) {
        palloc_free_page(frame); break;
      }
    }
    if (page < new_end) {
      while (page > old_end) {
        page -= PGSIZE;
        void *frame = pagedir_get_page(t->pagedir, (void *)page);
        pagedir_clear_page(t->pagedir, (void *)page);
        palloc_free_page(frame);
      }
      return (void *)-1;
    }
  } else {
    for (uintptr_t page = new_end; page < old_end; page += PGSIZE) {
      void *frame = pagedir_get_page(t->pagedir, (void *)page);
      pagedir_clear_page(t->pagedir, (void *)page);
      palloc_free_page(frame);
    }
  }
  t->heap_break = (void *)next;
  return (void *)old;
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = (uint32_t*)f->esp;
  struct thread* t = thread_current();
  t->in_syscall = true;

  validate_buffer_in_user_region(args, sizeof(uint32_t));
  switch (args[0]) {
    case SYS_SBRK:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      f->eax = (uint32_t)syscall_sbrk((intptr_t)args[1]);
      break;

    case SYS_EXIT:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_exit((int)args[1]);
      break;

    case SYS_OPEN:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = (uint32_t)syscall_open((char*)args[1]);
      break;

    case SYS_WRITE:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = (uint32_t)syscall_write((int)args[1], (void*)args[2], (unsigned)args[3]);
      break;

    case SYS_READ:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = (uint32_t)syscall_read((int)args[1], (void*)args[2], (unsigned)args[3]);
      break;

    case SYS_CLOSE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_close((int)args[1]);
      break;

    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->in_syscall = false;
}
