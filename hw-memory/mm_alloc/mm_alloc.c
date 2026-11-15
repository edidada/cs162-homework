#include "mm_alloc.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* C99-compatible alignment for the fundamental types on the target ABI. */
typedef union block block;
union block {
  struct { size_t capacity; block *prev, *next; int available; } info;
  long double align_float;
  void *align_pointer;
  long long align_integer;
};
static block *head, *tail;
static int rounded(size_t n, size_t *out) {
  size_t unit = sizeof(block);
  if (n > SIZE_MAX - (unit - 1)) return 0;
  *out = (n + unit - 1) / unit * unit;
  return *out <= (size_t)INTPTR_MAX - sizeof(block);
}
static void split(block *b, size_t capacity) {
  if (b->info.capacity - capacity < 2 * sizeof(block)) return;
  block *rest = (block *)((char *)(b + 1) + capacity);
  rest->info.capacity = b->info.capacity - capacity - sizeof(block);
  rest->info.available = 1;
  rest->info.prev = b;
  rest->info.next = b->info.next;
  if (rest->info.next) rest->info.next->info.prev = rest;
  else tail = rest;
  b->info.next = rest;
  b->info.capacity = capacity;
}
static int adjacent(block *a, block *b) {
  return (char *)(a + 1) + a->info.capacity == (char *)b;
}
static void merge_next(block *b) {
  block *next = b->info.next;
  if (!next || !next->info.available || !adjacent(b, next)) return;
  b->info.capacity += sizeof(block) + next->info.capacity;
  b->info.next = next->info.next;
  if (b->info.next) b->info.next->info.prev = b;
  else tail = b;
}
void *mm_malloc(size_t size) {
  size_t capacity;
  if (!size || !rounded(size, &capacity)) return NULL;
  block *b;
  for (b = head; b; b = b->info.next)
    if (b->info.available && b->info.capacity >= capacity) break;
  if (b) split(b, capacity);
  else {
    /* Pad the actual break, rather than assuming another sbrk user aligned it. */
    void *current = sbrk(0);
    if (current == (void *)-1) return NULL;
    size_t alignment = __alignof__(block);
    size_t pad = (alignment - (uintptr_t)current % alignment) % alignment;
    size_t total = sizeof(block) + capacity;
    if (total > (size_t)INTPTR_MAX - pad) return NULL;
    void *memory = sbrk((intptr_t)(total + pad));
    if (memory == (void *)-1) return NULL;
    b = (block *)((char *)memory + pad);
    b->info.capacity = capacity;
    b->info.prev = tail;
    b->info.next = NULL;
    if (tail) tail->info.next = b;
    else head = b;
    tail = b;
  }
  b->info.available = 0;
  memset(b + 1, 0, size);
  return b + 1;
}
void mm_free(void *ptr) {
  if (!ptr) return;
  block *b = (block *)ptr - 1;
  b->info.available = 1;
  merge_next(b);
  if (b->info.prev && b->info.prev->info.available && adjacent(b->info.prev, b))
    merge_next(b->info.prev);
}
void *mm_realloc(void *ptr, size_t size) {
  if (!ptr) return mm_malloc(size);
  if (!size) { mm_free(ptr); return NULL; }
  /* Allocate first: failure must leave both original bytes and ownership intact. */
  void *new_ptr = mm_malloc(size);
  if (!new_ptr) return NULL;
  block *old = (block *)ptr - 1;
  memcpy(new_ptr, ptr, old->info.capacity < size ? old->info.capacity : size);
  mm_free(ptr);
  return new_ptr;
}
