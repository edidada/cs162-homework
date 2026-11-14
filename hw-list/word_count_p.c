/*
 * Implementation of the word_count interface using Pintos lists and pthreads.
 *
 * You may modify this file, and are expected to modify it.
 */

/*
 * Copyright © 2021 University of California, Berkeley
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PINTOS_LIST
#error "PINTOS_LIST must be #define'd when compiling word_count_lp.c"
#endif

#ifndef PTHREADS
#error "PTHREADS must be #define'd when compiling word_count_lp.c"
#endif

#include "word_count.h"
#include <limits.h>

static word_count_t* find_unlocked(struct list* list, const char* word) {
  for (struct list_elem* e=list_begin(list); e!=list_end(list); e=list_next(e)) {
    word_count_t* node=list_entry(e,word_count_t,elem);
    if (strcmp(node->word,word)==0) return node;
  }
  return NULL;
}
static bool less_list(const struct list_elem* a, const struct list_elem* b, void* aux) {
  bool (*less)(const word_count_t*,const word_count_t*)=aux;
  return less(list_entry(a,word_count_t,elem),list_entry(b,word_count_t,elem));
}
static void lock_words(word_count_list_t* list) {
  if (pthread_mutex_lock(&list->lock)) abort();
}
static void unlock_words(word_count_list_t* list) {
  if (pthread_mutex_unlock(&list->lock)) abort();
}
void init_words(word_count_list_t* wclist) {
  if (!wclist) return;
  list_init(&wclist->lst);
  if (pthread_mutex_init(&wclist->lock,NULL)) abort();
}
size_t len_words(word_count_list_t* wclist) {
  if (!wclist) return 0;
  lock_words(wclist);
  size_t size=list_size(&wclist->lst);
  unlock_words(wclist);
  return size;
}
word_count_t* find_word(word_count_list_t* wclist, char* word) {
  if (!wclist || !word) return NULL;
  lock_words(wclist);
  word_count_t* node=find_unlocked(&wclist->lst,word);
  unlock_words(wclist);
  return node;
}
word_count_t* add_word(word_count_list_t* wclist, char* word) {
  if (!word) return NULL;
  if (!wclist) { free(word); return NULL; }
  lock_words(wclist);
  word_count_t* node=find_unlocked(&wclist->lst,word);
  if (node) {
    free(word);
    if (node->count==INT_MAX) node=NULL;
    else ++node->count;
  } else {
    node=malloc(sizeof(*node));
    if (!node) free(word);
    else {
      node->word=word;
      node->count=1;
      list_push_back(&wclist->lst,&node->elem);
    }
  }
  unlock_words(wclist);
  return node;
}
void fprint_words(word_count_list_t* wclist, FILE* outfile) {
  if (!wclist || !outfile) return;
  lock_words(wclist);
  for (struct list_elem* e=list_begin(&wclist->lst); e!=list_end(&wclist->lst); e=list_next(e)) {
    word_count_t* node=list_entry(e,word_count_t,elem);
    fprintf(outfile,"%i\t%s\n",node->count,node->word);
  }
  unlock_words(wclist);
}
void wordcount_sort(word_count_list_t* wclist, bool less(const word_count_t*,const word_count_t*)) {
  if (!wclist || !less) return;
  lock_words(wclist);
  list_sort(&wclist->lst,less_list,less);
  unlock_words(wclist);
}
