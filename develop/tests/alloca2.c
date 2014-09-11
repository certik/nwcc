#include <stdio.h>


int
main() {
	int	i;

	for (i = 0; i < 10; ++i) {
		char	*p = __builtin_alloca(i+2);
		memcpy(p, "hello world hmm", i + 1);
		p[i+1] = 0;
		puts(p);
	}
}	
