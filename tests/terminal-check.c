#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
int main(void) {
  if (!isatty(0) || getpgrp() != getpid() || tcgetpgrp(0) != getpgrp() ||
      getpgid(getppid()) == getpgrp()) return 1;
  int signals[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};
  for (unsigned i = 0; i < sizeof signals / sizeof *signals; ++i) {
    struct sigaction action;
    if (sigaction(signals[i], NULL, &action) < 0 || action.sa_handler != SIG_DFL) return 2;
  }
  puts("terminal-check-ok");
  return 0;
}
