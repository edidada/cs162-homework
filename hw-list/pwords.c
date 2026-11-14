/*
 * Word count application with one thread per input file.
 *
 * You may modify this file in any way you like, and are expected to modify it.
 * Your solution must read each input file from a separate thread. We encourage
 * you to make as few changes as necessary.
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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>

#include "word_count.h"
#include "word_helpers.h"

struct input_job {
  const char* path;
  word_count_list_t* words;
  int error;
};
static void* read_file(void* argument) {
  struct input_job* job=argument;
  FILE* input=fopen(job->path,"r");
  if (!input) { perror(job->path); job->error=1; return NULL; }
  count_words(job->words,input);
  if (ferror(input)) job->error=1;
  if (fclose(input)) job->error=1;
  return NULL;
}
int main(int argc, char* argv[]) {
  word_count_list_t words;
  init_words(&words);
  int error=0;
  if (argc<=1) {
    count_words(&words,stdin);
    if (ferror(stdin)) error=1;
  } else {
    size_t count=(size_t)argc-1, launched=0;
    pthread_t* threads=calloc(count,sizeof(*threads));
    struct input_job* jobs=calloc(count,sizeof(*jobs));
    if (!threads || !jobs) error=1;
    else {
      for (size_t i=0;i<count;++i) {
        jobs[i]=(struct input_job){argv[i+1],&words,0};
        int result=pthread_create(&threads[i],NULL,read_file,&jobs[i]);
        if (result) { fprintf(stderr,"pthread_create: %s\n",strerror(result)); error=1; break; }
        ++launched;
      }
      for (size_t i=0;i<launched;++i) {
        if (pthread_join(threads[i],NULL)) abort();
        if (jobs[i].error) error=1;
      }
    }
    free(threads); free(jobs);
  }
  if (!error) {
    wordcount_sort(&words,less_count);
    fprint_words(&words,stdout);
    if (fflush(stdout)) error=1;
  }
  while (!list_empty(&words.lst)) {
    word_count_t* node=list_entry(list_pop_front(&words.lst),word_count_t,elem);
    free(node->word); free(node);
  }
  pthread_mutex_destroy(&words.lock);
  return error;
}
