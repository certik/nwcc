#include <stdio.h>

int	x = 6;

int
main() {
	int	x = 234;
	{
		extern int	x;
		if (x == 234) {
			puts("WRONG");
		} else if (x == 6) {
			puts("OK");
		} else {
			puts("???");
		}
	}
	return 0;
}

