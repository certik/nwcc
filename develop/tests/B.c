#include <stdio.h>

int
main(void) {
	struct foo { int x; int y; };
	struct foo;
	struct foo	f;
	struct foo	*p = &f;
	char		*cp, *cp2;

	printf("%d\n", &((struct foo *)0)->y);
	cp = (char *)&p->y;
	cp2 = (char *)&p->x;
	printf("&p->y - &p->x = %d\n", (int)(cp - cp2));
}	
		 

