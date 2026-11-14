/*

  Word Count using dedicated lists

*/

/*
Copyright © 2019 University of California, Berkeley

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <assert.h>
#include <limits.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

#include "word_count.h"

/* Global data structure tracking the words encountered */
WordCount *word_counts = NULL;

/* The maximum length of each word in a file */
#define MAX_WORD_LEN 64

/*
 * 3.1.1 Total Word Count
 *
 * Returns the total amount of words found in infile.
 * Useful functions: fgetc(), isalpha().
 */
int num_words(FILE* infile) {
  if (!infile) return -1;
  int total = 0, length = 0, c;
  do {
    c = fgetc(infile);
    if (c != EOF && isalpha((unsigned char)c)) {
      if (length < 2) ++length;
    } else {
      if (length > 1) { if (total == INT_MAX) return -1; ++total; }
      length = 0;
    }
  } while (c != EOF);
  return ferror(infile) ? -1 : total;
}

/*
 * 3.1.2 Word Frequency Count
 *
 * Given infile, extracts and adds each word in the FILE to `wclist`.
 * Useful functions: fgetc(), isalpha(), tolower(), add_word().
 * 
 * As mentioned in the spec, your code should not panic or
 * segfault on errors. Thus, this function should return
 * 1 in the event of any errors (e.g. wclist or infile is NULL)
 * and 0 otherwise.
 */
int count_words(WordCount **wclist, FILE *infile) {
  if (!wclist || !infile) return 1;
  char word[MAX_WORD_LEN + 1];
  size_t length = 0;
  int c;
  do {
    c = fgetc(infile);
    if (c != EOF && isalpha((unsigned char)c)) {
      if (length == MAX_WORD_LEN) return 1;
      word[length++] = (char)tolower((unsigned char)c);
    } else {
      if (length > 1) {
        word[length] = '\0';
        if (add_word(wclist, word)) return 1;
      }
      length = 0;
    }
  } while (c != EOF);
  return ferror(infile) ? 1 : 0;
}

/*
 * Comparator to sort list by frequency.
 * Useful function: strcmp().
 */
static bool wordcount_less(const WordCount *wc1, const WordCount *wc2) {
  return wc1->count != wc2->count ? wc1->count < wc2->count
                                  : strcmp(wc1->word, wc2->word) < 0;
}

static void free_words(void) {
  while (word_counts) {
    WordCount *next = word_counts->next;
    free(word_counts->word);
    free(word_counts);
    word_counts = next;
  }
}

// In trying times, displays a helpful message.
static int display_help(void) {
	printf("Flags:\n"
	    "--count (-c): Count the total amount of words in the file, or STDIN if a file is not specified. This is default behavior if no flag is specified.\n"
	    "--frequency (-f): Count the frequency of each word in the file, or STDIN if a file is not specified.\n"
	    "--help (-h): Displays this help message.\n");
	return 0;
}

/*
 * Handle command line flags and arguments.
 */
int main (int argc, char *argv[]) {

  // Count Mode (default): outputs the total amount of words counted
  bool count_mode = true;
  int total_words = 0;

  // Freq Mode: outputs the frequency of each word
  bool freq_mode = false;

  FILE *infile = NULL;

  // Variables for command line argument parsing
  int i;
  static struct option long_options[] =
  {
      {"count", no_argument, 0, 'c'},
      {"frequency", no_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
  };

  // Sets flags
  while ((i = getopt_long(argc, argv, "cfh", long_options, NULL)) != -1) {
      switch (i) {
          case 'c':
              count_mode = true;
              freq_mode = false;
              break;
          case 'f':
              count_mode = false;
              freq_mode = true;
              break;
          case 'h':
              return display_help();
          default:
              return 1;
      }
  }

  if (!count_mode && !freq_mode) {
    printf("Please specify a mode.\n");
    return display_help();
  }

  if (init_words(&word_counts)) return 1;
  int first = optind, last = argc;
  if (first == last) last = first + 1;
  for (int file = first; file < last; ++file) {
    bool use_stdin = optind == argc;
    infile = use_stdin ? stdin : fopen(argv[file], "r");
    if (!infile) { perror(argv[file]); free_words(); return 1; }
    int error = 0;
    if (count_mode) {
      int count = num_words(infile);
      if (count < 0 || total_words > INT_MAX - count) error = 1;
      else total_words += count;
    } else error = count_words(&word_counts, infile);
    if (!use_stdin && fclose(infile)) error = 1;
    if (error) { fprintf(stderr, "Unable to count input words\n"); free_words(); return 1; }
  }

  if (count_mode) {
    printf("The total number of words is: %i\n", total_words);
  } else {
    wordcount_sort(&word_counts, wordcount_less);

    printf("The frequencies of each word are: \n");
    fprint_words(word_counts, stdout);
}
  free_words();
  return ferror(stdout) ? 1 : 0;
}
