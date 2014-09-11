typedef __builtin_va_list va_list;
#include <stdio.h>
void
(x_fprintf)(FILE *fd, const char *fmt, ...) {
        va_list va;
        int rc;

        __builtin_va_start(va,fmt);
        rc = vfprintf(fd, fmt, va);
        __builtin_va_end(va);
        if (rc == (-1) || fflush(fd) == (-1)) {
                perror("vfprintf");
                exit(1);
        }
}

int
main() {
	x_fprintf(stdout, "hello wolrld %s, %d %ld\n",
		"hehe", "hm"[1], 34722777);	
}

