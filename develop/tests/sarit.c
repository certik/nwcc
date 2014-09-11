#include <stdio.h>


int
main() {
	static char	cbuf[10];
	static short	sbuf[10];
	static int	ibuf[10];

	static char	*cp = &cbuf[5];
	static short	*sp = &sbuf[5];
	static int	*ip = &ibuf[5];

	printf("%d, %d, %d\n",
		(int)(cp - cbuf),
		(int)(sp - sbuf),
		(int)(ip - ibuf));
	return 0;
}

