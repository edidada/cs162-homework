#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/resource.h>

static int print_limit(int resource, const char* label) {
    struct rlimit lim;
    if (getrlimit(resource, &lim) != 0) {
        perror("getrlimit");
        return 1;
    }
    if (lim.rlim_cur == RLIM_INFINITY) printf("%s: -1\n", label);
    else printf("%s: %" PRIuMAX "\n", label, (uintmax_t)lim.rlim_cur);
    return 0;
}

int main(void) {
    if (print_limit(RLIMIT_STACK, "stack size") ||
        print_limit(RLIMIT_NPROC, "process limit") ||
        print_limit(RLIMIT_NOFILE, "max file descriptors")) return 1;
    return fflush(stdout) == EOF ? 1 : 0;
}
