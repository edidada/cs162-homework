#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm_alloc.h"
static unsigned state = 162;
static unsigned random_value(void) { state = state * 1664525u + 1013904223u; return state; }
static void check(const unsigned char *p, size_t n, unsigned char value) {
  for (size_t i = 0; i < n; ++i) assert(p[i] == value);
}
int main(void) {
  assert(!mm_malloc(0)); mm_free(NULL);
  assert(!mm_realloc(NULL, 0));
  assert(!mm_malloc(SIZE_MAX)); assert(!mm_malloc((size_t)INTPTR_MAX));
  unsigned char *a = mm_malloc(4096), *b = mm_malloc(4096), *c = mm_malloc(4096);
  assert(a && b && c); check(a,4096,0); memset(a,37,4096);
  assert((uintptr_t)a % __alignof__(long double) == 0);
  assert(!mm_realloc(a,SIZE_MAX)); check(a,4096,37);
  mm_free(b); void *brk = sbrk(0);
  unsigned char *small = mm_malloc(128); assert(small == b); check(small,128,0);
  unsigned char *rest = mm_malloc(128); assert(rest > small && rest < c);
  assert(sbrk(0)==brk); mm_free(small); mm_free(rest);
  mm_free(a); mm_free(c);
  unsigned char *large = mm_malloc(12000); assert(large==a); assert(sbrk(0)==brk);
  check(large,12000,0); mm_free(large);
  struct slot { unsigned char *p; size_t n; unsigned char value; } slots[64] = {{0}};
  for (unsigned step=0; step<20000; ++step) {
    struct slot *s = &slots[random_value()%64];
    if(s->p) check(s->p,s->n,s->value);
    unsigned action = random_value()%3;
    if(action==0) { mm_free(s->p); s->p=NULL; s->n=0; }
    else {
      size_t n=random_value()%2048;
      unsigned char *p= action==1 ? mm_realloc(s->p,n) : mm_malloc(n);
      assert(p || !n);
      if(action==1 && s->p && p) check(p,n<s->n?n:s->n,s->value);
      if(action==2) { if(p) check(p,n,0); mm_free(s->p); }
      s->p=p; s->n=n; s->value=(unsigned char)random_value();
      if(p) memset(p,s->value,n);
    }
    for(unsigned i=0;i<64;++i) if(slots[i].p) check(slots[i].p,slots[i].n,slots[i].value);
  }
  for(unsigned i=0;i<64;++i) mm_free(slots[i].p);
  puts("allocator edge/split/coalesce/alignment/failure checks and 20000 randomized operations passed");
}
