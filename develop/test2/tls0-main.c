#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>


extern int	threads;
extern void	*thread_func(void *);

static int
read_line(int fd, char *buf) {
	char	ch;
	char	*p = buf;

	for (;;) {
		int	rc;
		if ((rc = read(fd, &ch, 1)) == -1) {
			return -1;
		} else if (rc == 0 || ch == '\n') {
			*p = 0;
			break;
		} else {
			*p++ = ch;
		}
	}
	return p - buf;
}


int
main() {
	int	i;
	int	pfds[2];

	threads = 5;

	if (pipe(pfds) == -1) {
		perror("pipe");
		return EXIT_FAILURE;
	}

	switch (fork()) {
	case -1:
		perror("fork");
		return EXIT_FAILURE;
	case 0:
		/* Child writes to parent */
		(void) close(pfds[STDIN_FILENO]);
		(void) dup2(pfds[STDOUT_FILENO], STDOUT_FILENO);
		break;
	default:
		/* Parent reads data, sorts it, prints it */ 
		(void) close(pfds[STDOUT_FILENO]);
		{
			char	buf[128];
			int	ar[128] = { 0 };

			for (i = 0; i < threads; ++i) {
				int	rc = read_line(pfds[STDIN_FILENO], buf);

				if (rc == -1) {
					perror("read");
					return EXIT_FAILURE;
				} else if (rc == 0) {
					puts("unexpected EOF");
					return EXIT_FAILURE;
				} else {
					ar[i] = atoi(buf);
				}
			}
			for (i = 0; i < threads; ++i) {
				int	j;

				for (j = i + 1; j < threads; ++j) {
					if (ar[i] < ar[j]) {
						int	temp = ar[i];
						ar[i] = ar[j];
						ar[j] = temp;
					}
				}
			}
			for (i = 0; i < threads; ++i) {
				printf("%d: %d\n", i, ar[i]);
			}
		}
		_exit(0);
	}
		

	for (i = 0; i < threads; ++i) {
		pthread_t	tid;
		(void) pthread_create(&tid, NULL, thread_func, NULL);
	}
	pthread_exit(NULL);
}

