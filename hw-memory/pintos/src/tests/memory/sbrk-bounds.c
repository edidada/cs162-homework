#include <stdint.h>
#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"
int main(int argc UNUSED, char **argv UNUSED) {
  test_name = "sbrk-bounds";
  msg("begin");
  unsigned char *start = sbrk(0);
  ASSERT(start != (void *)-1);
  ASSERT((uintptr_t)start % 4096 == 0);
  ASSERT(sbrk(-1) == (void *)-1);
  ASSERT(sbrk(INTPTR_MIN) == (void *)-1);
  ASSERT(sbrk(0) == start);
  ASSERT(sbrk(4097) == start);
  memset(start, 73, 4097);
  ASSERT(sbrk(INTPTR_MIN) == (void *)-1);
  ASSERT(sbrk(INTPTR_MAX) == (void *)-1);
  ASSERT(sbrk(-4098) == (void *)-1);
  ASSERT(sbrk(0) == start + 4097);
  for (int i = 0; i < 4097; ++i) ASSERT(start[i] == 73);
  ASSERT(sbrk(-1) == start + 4097);
  ASSERT(sbrk(1) == start + 4096);
  ASSERT(start[4096] == 0); /* Whole second page was released and zero-remapped. */
  ASSERT(sbrk(-4097) == start + 4097);
  ASSERT(sbrk(0) == start);
  ASSERT(sbrk(8192) == start);
  for (int i = 0; i < 8192; ++i) ASSERT(start[i] == 0);
  ASSERT(sbrk(-8192) == start + 8192);
  msg("end");
  return 0;
}
