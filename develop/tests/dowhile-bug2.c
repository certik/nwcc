#include <stdio.h>

int
main() {
	int	foo;
	char	buf[128];
	char	*p;
	int	i;

	do
		if (1)
			for (i = 0; i < 1; ++i) { 
				puts("well");
			}	
		else
			puts("mm");
	while (0);

	do
		if (0)
			for (i = 0; i < 1; ++i) { 
				puts("well");
			}	
		else
			puts("mm");
	while (0);
}

