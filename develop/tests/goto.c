#include <stdio.h>

int
main(void) {
	int	ch;
	int	done = 0;

	while ((ch = getchar()) != EOF) {
		if (isdigit((unsigned char)ch)) {
			done = 1;
			goto done;
		}
	}
done:
	if (done) putchar(ch);
	else puts("premature end of loop");
	return 0;
}

