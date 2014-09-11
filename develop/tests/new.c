#include <stdio.h>

int
main(void) {
	char	buf[10];
	int	i = 0;
	int	ch;

	while ((ch = getchar()) != EOF) {
		if (i == 9) {
			puts("Truncating text");
			break;
		}	
		buf[i] = ch;
		++i;
	}
	buf[i] = 0;
	printf("text: %s", buf);
	return 0;
}

