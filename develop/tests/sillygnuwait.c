#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

int		global_status;
static int	filescope_status;

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
		if (((((__extension__((
			(union {
				__typeof(status) __in;
				int __i;
			}) {
				.__in = (status)
			}).__i))) & 0xff00) >> 8) != 5) {
			puts("BUG");
		} else {
			puts("OK");
		}

		global_status = filescope_status = status;

		if (((((__extension__((
			(union {
				__typeof(global_status) __in;
				int __i;
			}) {
				.__in = (global_status)
			}).__i))) & 0xff00) >> 8) != 5) {
			puts("BUG");
		} else {
			puts("OK");
		}

		if (((((__extension__((
			(union {
				__typeof(filescope_status) __in;
				int __i;
			}) {
				.__in = (filescope_status)
			}).__i))) & 0xff00) >> 8) != 5) {
			puts("BUG");
		} else {
			puts("OK");
		}	
	}
}	



