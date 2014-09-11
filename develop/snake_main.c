/*
 * Snake - written by nrw on Dec 28 2005
 */
#include "snake_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


static long 
getval(const char *prompt) {
	char	buf[128];
	char	*p;
	long	res;

	do {
		printf("%s: ", prompt);
		fflush(stdout);
		if (fgets(buf, sizeof buf, stdin) == NULL) {
			puts("Goodbye");
			exit(0);
		}
		if ((p = strchr(buf, '\n')) != NULL) {
			*p = 0;
		} else {
			int	ch;
			do ch = getchar(); while (ch != EOF && ch != '\n');
		}
		res = strtol(buf, &p, 10);
	} while ((*p || p == buf || res == LONG_MIN || res == LONG_MAX)
		&& puts("Bad input"));
	return res;	
}

int
main(void) {
	long	speed;
	long	delay;

	puts("snake 0.1 - written on Dec 28 2005");
	puts("(this game is pretty incomplete, much like nwcc itself!)");
	puts("Controls: left = a, right = d, up = w, down = s");
	puts("");
	puts("By the way, it is OK to hit the end of the screen; The");
	puts("snake will wrap around");
	puts("");


	do {
		speed = getval("Enter the speed of the snake "
				"(1 = max, 10 = min (recommended: 3 or 4))");
	} while (speed < 1 || speed > 10);

	delay = speed * 15000; 

	return driver(delay);
}

