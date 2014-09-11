#include <stdio.h>
#include <pthread.h>

static __thread int	et;

static int	counter;
static pthread_mutex_t	cmut = PTHREAD_MUTEX_INITIALIZER;

int	threads;
int	g_done;

pthread_mutex_t	comm = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	cond = PTHREAD_COND_INITIALIZER;

static void
set_value(int val) {
	et = val;
}

static int
get_value() {
	return et;
}

void *
thread_func(void *arg) {
	int	i;
	int	done = 0;

	/*
	 * Set thread-specific value
	 */
	pthread_mutex_lock(&cmut);
	i = counter++;
	if (counter == threads) {
		done = 1;
	}
	pthread_mutex_unlock(&cmut);
	set_value(i);

	pthread_mutex_lock(&comm);
	for (;;) {
		if (done) {
			g_done = 1;
			pthread_cond_broadcast(&cond);
			pthread_mutex_unlock(&comm);
			break;
		} else {
			if (g_done) {
				pthread_mutex_unlock(&comm);
				break;
			}
			pthread_cond_wait(&cond, &comm);
		}
	}
	printf("%d\n", get_value());
}

