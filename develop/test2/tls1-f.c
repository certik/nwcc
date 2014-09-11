#include <stdio.h>
#include <string.h>

#if 0
#include <pthread.h>

static __thread struct ets {
	int	val;
	char	buf[128];
	float	x;
} et;

static int	counter;
static pthread_mutex_t	cmut = PTHREAD_MUTEX_INITIALIZER;

int	threads;
int	g_done;

pthread_mutex_t	comm = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	cond = PTHREAD_COND_INITIALIZER;

static void
set_value(struct ets *val) {
	memcpy(&et, val, sizeof et);
}

static struct ets * 
get_value() {
	return &et;
}
#endif


extern __thread int	tvar;

void *
thread_func(void *arg) {
tvar = 0;
#if 0
	int			i;
	int			done = 0;
	struct ets		e;
	struct ets		*ep;
	static const char	*str[] = {
		"zero",
		"one",
		"two",
		"three",
		"four",
		"five",
		"six",
		"seven"
	};

	/*
	 * Set thread-specific value
	 */
	pthread_mutex_lock(&cmut);
	i = counter++;
	if (i == 0) {
		printf("The other compiler said: %d\n", tvar);
	}
	if (counter == threads) {
		done = 1;
	}
	pthread_mutex_unlock(&cmut);


	e.val = i;
	strcpy(e.buf, str[i]);
	e.x = i / 2.f;
	set_value(&e);


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
	ep = get_value();
	printf("%d %s %f\n", ep->val, ep->buf, ep->x);
	return NULL;
	#endif
}

