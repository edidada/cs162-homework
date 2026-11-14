#include "word_count.h"
#include <assert.h>
static bool reverse(const word_count_t* a,const word_count_t* b) { return strcmp(a->word,b->word)>0; }
#ifdef PTHREADS
static void* work(void* p) {
  for(int i=0;i<500;++i) assert(add_word(p,strdup("shared")));
  return NULL;
}
#endif
int main(void) {
  word_count_list_t words; init_words(&words);
  assert(len_words(&words)==0 && !find_word(&words,"absent"));
  assert(add_word(&words,strdup("alpha")));
  assert(add_word(&words,strdup("beta")));
  assert(add_word(&words,strdup("alpha"))->count==2);
  assert(len_words(&words)==2);
#ifdef PTHREADS
  pthread_t threads[8];
  for(int i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,work,&words));
  for(int i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
  assert(find_word(&words,"shared")->count==4000 && len_words(&words)==3);
  struct list* lst=&words.lst;
#else
  struct list* lst=&words;
#endif
  wordcount_sort(&words,reverse);
  for(struct list_elem* e=list_begin(lst);e!=list_end(lst);e=list_next(e)) {
    struct list_elem* next=list_next(e);
    if(next!=list_end(lst)) assert(reverse(list_entry(e,word_count_t,elem),list_entry(next,word_count_t,elem)));
  }
  while(!list_empty(lst)) { word_count_t* n=list_entry(list_pop_front(lst),word_count_t,elem); free(n->word); free(n); }
#ifdef PTHREADS
  pthread_mutex_destroy(&words.lock);
#endif
  puts("generic comparator, ownership, duplicate and list boundaries passed; parallel variant includes 4000 shared increments");
}
