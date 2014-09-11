#include <stdio.h>

int
main() {
	int	foo;
	char	buf[128];
	char	*p;
	int	i;

	do
		puts("HELLO");
	while (0);

	do
		if (0)
			puts("looks like a bug");
		else if (0)
			for (i = 0; i < 1; ++i)
				puts("nope, still buggy");
		else if (1)
			for (i = 0; i < 1; ++i) {
				puts("looking good");
			}	
		else puts("mm");
	while (0);

	do
		if (1) {
			puts("loooooooooooooooooooooool");
		}
	while (0);

	do
		for (i = 0; i < 3; ++i)
			puts("hmm");
	while (0);

	i = 0;
	do
		for (++i; i < 3; ++i) {
			puts("hehe");
		}
	while (i == 3);

	do if ((i = 0) == 0) { puts("GOOD"); } while (0);

	i = 0;
	
	for (; i < 5; ++i) {
		p = buf;
		if ((foo = i) != 0)
			do {
				*p++ = '0' + foo;
			} while (++foo <= 9);
		else
			puts("woah this sux");
		strcpy(p, "\n");
		puts(buf);
	}
	return 0;
}

