#include "word_count.h"
#include <assert.h>
#include <limits.h>
int num_words(FILE*);
int count_words(WordCount**,FILE*);
void* __real_malloc(size_t);
static int fail_after=-1;
void* __wrap_malloc(size_t size) {
  if (fail_after==0) return NULL;
  if (fail_after>0) --fail_after;
  return __real_malloc(size);
}
int main(void) {
  WordCount* list=NULL;
  assert(init_words(NULL)==1);
  assert(init_words(&list)==0 && len_words(list)==0);
  assert(add_word(NULL,"hello")==1 && add_word(&list,NULL)==1);
  assert(find_word(list,NULL)==NULL);
  assert(num_words(NULL)==-1 && count_words(NULL,stdin)==1 && count_words(&list,NULL)==1);
  fail_after=0; assert(add_word(&list,"hello")==1 && list==NULL);
  fail_after=1; assert(add_word(&list,"hello")==1 && list==NULL);
  fail_after=-1;
  char buffer[]="hello";
  assert(add_word(&list,buffer)==0);
  buffer[0]='X'; assert(find_word(list,"hello")!=NULL);
  assert(add_word(&list,"hello")==0 && list->count==2 && len_words(list)==1);
  list->count=INT_MAX; assert(add_word(&list,"hello")==1 && list->count==INT_MAX);
  fprint_words(list,NULL);
  free(list->word); free(list);
  puts("API null/allocation-failure/ownership/duplicate/overflow checks passed");
}
