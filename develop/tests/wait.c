#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

/*
 * The point of this test is primarily to test
 * __attribute__((__transparent_union__)); This is used on some GNU-
 * aware systems to declare the wait() function
 */
int
main() {
	pid_t	pid;

	if ((pid = fork()) == (pid_t)-1) {
		perror("fork");
		return 1;
	} else if (pid == 0) {
		_exit(5);
	} else {
		int	status;

		wait(&status);
		if (WEXITSTATUS(status) != 5) {
			puts("BUG");
		} else {
			puts("OK");
		}
	}
	return 0;
}

