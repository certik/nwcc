#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void *xmalloc (size_t gnus)
{
	void	*p = malloc(gnus);

	if (p == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return p;
}

char *
lolalloc(size_t n)
{
	return 1 ? xmalloc (n) : 0;
}

char *
lmaoalloc(size_t n)
{
	return 0 ? xmalloc (n) : 0;
}

int
main() {
	static char *(*funcs[2])(size_t) = {
		lolalloc,
		lmaoalloc
	};
	int	i;
	for (i = 0; i < sizeof funcs / sizeof funcs[0]; ++i) {
		void	*p;

		p = funcs[i](16);
		if (p == NULL) {
			printf("%d: NULL\n", i);
		} else {
			strcpy(p, "hello world");
			printf("%d: %s\n", i, p);
			free(p);
		}
	}
	return 0;
}
			
