#include <stdio.h>

int
main() {
	struct foo {
		char	*bar;
	} f, *fp = &f;

	printf("%d %d %d %d\n",
		sizeof(f.bar) , sizeof(fp->bar),
		sizeof(&f.bar[7]), sizeof(++fp->bar)); 
	printf("%d %d %d %d %d\n",
		sizeof(0? (void *)0: f.bar), sizeof(!!&f),
	      sizeof(fp - &f) , sizeof(fp - 1) ,
		sizeof(fp + 1));
	printf("%d\n", sizeof printf("lol"));
}	
	
