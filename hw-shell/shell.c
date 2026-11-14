#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "tokenizer.h"

/* A pipeline is one terminal job. Each member is tracked until reaped. */
struct member { pid_t pid; bool done, stopped; };
struct job {
  pid_t pgid;
  size_t n;
  struct member *members;
  struct termios modes;
  bool have_modes;
  struct job *next;
};
static struct job *jobs;
static bool interactive;
static pid_t shell_pgid;
static struct termios shell_modes;
static const int signals[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};

static void disposition(int how) {
  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_handler = how ? SIG_IGN : SIG_DFL;
  sigemptyset(&action.sa_mask);
  for (size_t i = 0; i < sizeof signals / sizeof *signals; ++i)
    if (sigaction(signals[i], &action, NULL) < 0) { perror("sigaction"); exit(1); }
}
static bool finished(struct job *j) {
  for (size_t i = 0; i < j->n; ++i) if (!j->members[i].done) return false;
  return true;
}
static bool stopped(struct job *j) {
  bool live = false;
  for (size_t i = 0; i < j->n; ++i) {
    if (j->members[i].done) continue;
    live = true;
    if (!j->members[i].stopped) return false;
  }
  return live;
}
static void record(pid_t pid, int status) {
  for (struct job *j = jobs; j; j = j->next)
    for (size_t i = 0; i < j->n; ++i) if (j->members[i].pid == pid) {
      if (WIFEXITED(status) || WIFSIGNALED(status)) j->members[i].done = true;
      else if (WIFSTOPPED(status)) j->members[i].stopped = true;
      else if (WIFCONTINUED(status)) j->members[i].stopped = false;
      return;
    }
}
static void collect(void) {
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    record(pid, status);
  struct job **link = &jobs;
  while (*link) {
    struct job *j = *link;
    if (finished(j)) { *link = j->next; free(j->members); free(j); }
    else link = &j->next;
  }
}
static void await_job(struct job *j) {
  while (!finished(j) && !stopped(j)) {
    int status;
    pid_t pid = waitpid(-j->pgid, &status, WUNTRACED | WCONTINUED);
    if (pid > 0) record(pid, status);
    else if (pid < 0 && errno == EINTR) continue;
    else { if (pid < 0) perror("waitpid"); break; }
  }
}
static void foreground(struct job *j, bool resume) {
  if (interactive) {
    if (tcsetpgrp(STDIN_FILENO, j->pgid) < 0) perror("tcsetpgrp job");
    if (resume && j->have_modes)
      if (tcsetattr(STDIN_FILENO, TCSADRAIN, &j->modes) < 0) perror("tcsetattr job");
  }
  if (resume) {
    for (size_t i = 0; i < j->n; ++i) j->members[i].stopped = false;
    if (kill(-j->pgid, SIGCONT) < 0) perror("SIGCONT");
  }
  await_job(j);
  if (interactive) {
    if (tcgetattr(STDIN_FILENO, &j->modes) == 0) j->have_modes = true;
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) perror("tcsetpgrp shell");
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_modes) < 0) perror("tcsetattr shell");
  }
}
static void init_shell(void) {
  interactive = isatty(STDIN_FILENO);
  if (!interactive) return;
  shell_pgid = getpgrp();
  while (tcgetpgrp(STDIN_FILENO) != shell_pgid) {
    kill(-shell_pgid, SIGTTIN);
    shell_pgid = getpgrp();
  }
  disposition(1);
  shell_pgid = getpid();
  if (setpgid(0, shell_pgid) < 0 && errno != EACCES && errno != EPERM) {
    perror("setpgid shell"); exit(1);
  }
  if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0 ||
      tcgetattr(STDIN_FILENO, &shell_modes) < 0) { perror("terminal"); exit(1); }
}
/* Empty PATH components denote the current directory; slash names bypass PATH. */
static void execute(char **argv) {
  if (strchr(argv[0], '/')) execv(argv[0], argv);
  else {
    const char *path = getenv("PATH");
    if (!path) path = "/bin:/usr/bin";
    int saved = ENOENT;
    const char *p = path;
    do {
      const char *end = strchr(p, ':');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      char *candidate = malloc(len + strlen(argv[0]) + 2);
      if (!candidate) { perror("malloc"); _exit(126); }
      if (len) { memcpy(candidate, p, len); candidate[len] = '/'; }
      strcpy(candidate + len + (len != 0), argv[0]);
      execv(candidate, argv);
      int error = errno;
      free(candidate);
      if (error != ENOENT && error != ENOTDIR) saved = error;
      if (!end) break;
      p = end + 1;
    } while (true);
    errno = saved;
  }
  int error = errno;
  perror(argv[0]);
  _exit(error == ENOENT ? 127 : 126);
}
struct command { char **argv; size_t argc; char *in, *out; };
static bool special(const char *s) {
  return !strcmp(s, "|") || !strcmp(s, "<") || !strcmp(s, ">") || !strcmp(s, "&");
}
static void free_commands(struct command *c, size_t n) {
  for (size_t i = 0; i < n; ++i) free(c[i].argv);
  free(c);
}
static struct command *parse(struct tokens *t, size_t *n, bool *background) {
  size_t len = tokens_get_length(t);
  *n = 1; *background = false;
  if (len && !strcmp(tokens_get_token(t, len - 1), "&")) { *background = true; --len; }
  struct command *c = calloc(len + 1, sizeof *c);
  if (!c) return NULL;
  for (size_t i = 0; i <= len; ++i) {
    c[i].argv = calloc(len + 1, sizeof(char *));
    if (!c[i].argv) { free_commands(c, i); return NULL; }
  }
  size_t stage = 0;
  for (size_t i = 0; i < len; ++i) {
    char *s = tokens_get_token(t, i);
    if (!strcmp(s, "|")) {
      if (!c[stage].argc) goto invalid;
      ++stage;
    } else if (!strcmp(s, "<") || !strcmp(s, ">")) {
      bool input = *s == '<';
      if (++i == len || special(tokens_get_token(t, i))) goto invalid;
      if (input) c[stage].in = tokens_get_token(t, i);
      else c[stage].out = tokens_get_token(t, i);
    } else if (!strcmp(s, "&")) goto invalid;
    else c[stage].argv[c[stage].argc++] = s;
  }
  if (!c[stage].argc) goto invalid;
  *n = stage + 1;
  /* Free unused parser slots; stage argv arrays remain owned by c. */
  for (size_t i = *n; i <= len; ++i) free(c[i].argv);
  return c;
invalid:
  fprintf(stderr, "invalid command\n");
  free_commands(c, len + 1);
  return NULL;
}
static int redirect(const char *name, int target) {
  if (!name) return 0;
  int fd = open(name, target == STDIN_FILENO ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) { perror(name); return -1; }
  if (dup2(fd, target) < 0) { perror("dup2"); close(fd); return -1; }
  if (fd != target) close(fd);
  return 0;
}
static void launch(struct command *c, size_t n, bool background) {
  struct job *j = calloc(1, sizeof *j);
  if (!j) { perror("calloc"); return; }
  j->members = calloc(n, sizeof *j->members);
  if (!j->members) { free(j); perror("calloc"); return; }
  /* A gate prevents readers racing terminal transfer, and children escaping
     their process group before every pipeline member has joined it. */
  int gate[2];
  if (pipe(gate) < 0) { perror("pipe"); free(j->members); free(j); return; }
  j->next = jobs; jobs = j;
  int previous = -1;
  fflush(NULL);
  bool failed = false;
  for (size_t i = 0; i < n; ++i) {
    int next[2] = {-1, -1};
    if (i + 1 < n && pipe(next) < 0) { perror("pipe"); failed = true; break; }
    pid_t pid = fork();
    if (pid == 0) {
      close(gate[1]);
      if (setpgid(0, j->pgid ? j->pgid : getpid()) < 0) { perror("setpgid child"); _exit(1); }
      disposition(0);
      char byte;
      while (read(gate[0], &byte, 1) < 0) if (errno != EINTR) _exit(1);
      close(gate[0]);
      if (previous >= 0 && dup2(previous, STDIN_FILENO) < 0) _exit(1);
      if (next[1] >= 0 && dup2(next[1], STDOUT_FILENO) < 0) _exit(1);
      if (previous >= 0) close(previous);
      if (next[0] >= 0) close(next[0]);
      if (next[1] >= 0) close(next[1]);
      if (redirect(c[i].in, STDIN_FILENO) < 0 || redirect(c[i].out, STDOUT_FILENO) < 0) _exit(1);
      execute(c[i].argv);
    }
    if (pid < 0) {
      perror("fork");
      if (next[0] >= 0) close(next[0]);
      if (next[1] >= 0) close(next[1]);
      failed = true; break;
    }
    if (!j->pgid) j->pgid = pid;
    j->members[j->n++].pid = pid;
    if (setpgid(pid, j->pgid) < 0) perror("setpgid parent");
    if (previous >= 0) close(previous);
    if (next[1] >= 0) close(next[1]);
    previous = next[0];
  }
  if (previous >= 0) close(previous);
  close(gate[0]);
  if (failed && j->pgid) kill(-j->pgid, SIGKILL);
  if (!failed && !background && interactive)
    if (tcsetpgrp(STDIN_FILENO, j->pgid) < 0) perror("tcsetpgrp launch");
  close(gate[1]);
  if (failed) await_job(j);
  else if (!background) foreground(j, false);
  collect();
}
static struct job *select_job(struct command *c) {
  if (c->argc == 1) return jobs;
  if (c->argc != 2) return NULL;
  char *end;
  errno = 0;
  long pid = strtol(c->argv[1], &end, 10);
  if (errno || *end || pid <= 0) return NULL;
  for (struct job *j = jobs; j; j = j->next)
    for (size_t i = 0; i < j->n; ++i) if (pid == j->members[i].pid) return j;
  return NULL;
}
static bool builtin(struct command *c, bool *quit) {
  char *name = c->argv[0];
  if (!strcmp(name, "exit")) *quit = true;
  else if (!strcmp(name, "?")) puts("? - help\nexit - exit shell\npwd - working directory\ncd PATH - change directory\nwait - wait for background jobs\nfg [pid] - foreground job\nbg [pid] - resume job");
  else if (!strcmp(name, "pwd")) {
    char *cwd = getcwd(NULL, 0);
    if (cwd) { puts(cwd); free(cwd); } else perror("pwd");
  } else if (!strcmp(name, "cd")) {
    if (c->argc != 2) fprintf(stderr, "usage: cd PATH\n");
    else if (chdir(c->argv[1]) < 0) perror("cd");
  } else if (!strcmp(name, "wait")) {
    /* Unlike a foreground prompt wait, this builtin waits for termination,
       including jobs that must first be resumed by an external process. */
    for (struct job *j = jobs; j; j = j->next) {
      while (!finished(j)) {
        int status;
        pid_t pid = waitpid(-j->pgid, &status, 0);
        if (pid > 0) record(pid, status);
        else if (pid < 0 && errno == EINTR) continue;
        else { perror("waitpid"); break; }
      }
    }
    collect();
  } else if (!strcmp(name, "fg") || !strcmp(name, "bg")) {
    struct job *j = select_job(c);
    if (!j) fprintf(stderr, "%s: no matching job\n", name);
    else if (*name == 'f') foreground(j, true);
    else {
      for (size_t i = 0; i < j->n; ++i) j->members[i].stopped = false;
      if (kill(-j->pgid, SIGCONT) < 0) perror("bg");
    }
    collect();
  } else return false;
  return true;
}
int main(void) {
  init_shell();
  /* Prevent stdio read-ahead from stealing subsequent noninteractive input
     from a program which inherits the shell's input descriptor. */
  setvbuf(stdin, NULL, _IONBF, 0);
  char line[4096];
  unsigned long number = 0;
  bool quit = false;
  while (!quit) {
    collect();
    if (interactive) { printf("%lu: ", number++); fflush(stdout); }
    if (!fgets(line, sizeof line, stdin)) break;
    if (!strchr(line, '\n') && !feof(stdin)) {
      int ch; while ((ch = getchar()) != '\n' && ch != EOF) {}
      fprintf(stderr, "command line too long\n"); continue;
    }
    struct tokens *t = tokenize(line);
    if (tokens_get_length(t)) {
      size_t n; bool background;
      struct command *c = parse(t, &n, &background);
      if (c) {
        if (!(n == 1 && builtin(c, &quit))) launch(c, n, background);
        free_commands(c, n);
      }
    }
    tokens_destroy(t);
  }
  /* Exiting a shell must not leave its stopped jobs indefinitely suspended. */
  for (struct job *j = jobs; j;) {
    kill(-j->pgid, SIGHUP); kill(-j->pgid, SIGCONT);
    struct job *next = j->next;
    free(j->members); free(j); j = next;
  }
  return 0;
}
