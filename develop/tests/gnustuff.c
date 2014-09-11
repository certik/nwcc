//#include <stdio.h>

#define LOL(x, y) (x? : y)

int
main() {
#if 0
	int	i = 128;
#endif
	struct foo {
		int	x;
		union {
			int	y;
			int	y_alias;
			char	*p;
		};
		int	z;
		union {
			int	lol;
		};
		int	end;
	} bar;
	struct foo	*barp = &bar;

#if 0
	/* Test conditionals with omitted operands */
	printf("%d\n", LOL(i++, 256));
	printf("%d\n", i);
	i = 0;
	printf("%d\n", LOL(i++, 256));
	printf("%d\n", i);
	return 0;
#endif

	/* Test anonymous unions */
	bar.x = 1;
	bar.y = 2;
	bar.z = 3;
	bar.lol = 4;
	bar.end = 5;

	printf("size = %d\n", (int)sizeof(struct foo));
	printf("first union begins at %u\n",
		(unsigned)((char *)&bar.y - (char *)&bar));
	printf("second union begins at %u\n",
		(unsigned)((char *)&bar.lol - (char *)&bar));	

	printf("values = %d,%d,%d,%d,%d\n",
		bar.x, bar.y, bar.z, bar.lol, bar.end);	
	barp->p = "hello world!";
	printf("values = %d,%s,%d,%d,%d\n",
		bar.x, bar.p, bar.z, bar.lol, bar.end);	
}


