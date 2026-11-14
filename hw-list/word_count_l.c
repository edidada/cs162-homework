/*
 * Implementation of the word_count interface using Pintos lists.
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
#error "PINTOS_LIST must be #define'd when compiling word_count_l.c"
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
void init_words(word_count_list_t* wclist) {
  if (!wclist) return;
  list_init(wclist);
}
size_t len_words(word_count_list_t* wclist) {
  if (!wclist) return 0;
  size_t size=list_size(wclist);
  return size;
}
word_count_t* find_word(word_count_list_t* wclist, char* word) {
  if (!wclist || !word) return NULL;
  word_count_t* node=find_unlocked(wclist,word);
  return node;
}
word_count_t* add_word(word_count_list_t* wclist, char* word) {
  if (!word) return NULL;
  if (!wclist) { free(word); return NULL; }
  word_count_t* node=find_unlocked(wclist,word);
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
      list_push_back(wclist,&node->elem);
    }
  }
  return node;
}
void fprint_words(word_count_list_t* wclist, FILE* outfile) {
  if (!wclist || !outfile) return;
  for (struct list_elem* e=list_begin(wclist); e!=list_end(wclist); e=list_next(e)) {
    word_count_t* node=list_entry(e,word_count_t,elem);
    fprintf(outfile,"%i\t%s\n",node->count,node->word);
  }
}
void wordcount_sort(word_count_list_t* wclist, bool less(const word_count_t*,const word_count_t*)) {
  if (!wclist || !less) return;
  list_sort(wclist,less_list,less);
}
