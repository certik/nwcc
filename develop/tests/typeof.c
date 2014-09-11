#if 0
#include <stdio.h>
#endif

int
main(void) {
	char	*lol[2];
	unsigned long	stuff = 2134633;
	struct bogus {
		char	big[9999];
	} b;	
	typedef union lolmonster {
		char	x;
		int	z;
		double	y;
		short	z2;
	} lolmonster_t;

	typeof(123)	x = 5;
	typeof(*lol)	p = "Hello";
	typeof(b)	bla;
	typeof(&b)	blap;
	
	printf("%s: %d\n", p, x);
	blap = &bla;
	blap->big[9] = 123;
	printf("%d\n", bla.big[9]);

#if 0
	/* XXX */
	printf("alignment of long long is %d\n", __alignof__(long long));
#endif
	printf("alignment of struct bogus: %d\n", __alignof(b));

	if (__builtin_expect(123 != 456, 0)) {
		puts(":)");
	}
	if (__builtin_expect(!555, 1)) {
		puts(":(");
	} else {
		puts(":)");
	}	
	{
		int	x = __builtin_expect(555555, 0);
		printf("%d\n", x);
	}	
#if 0
	printf("alignment of lolmonster: %d\n", __alignof__(lolmonster_t));
#endif

	printf("%d\n", (__typeof__(unsigned char))stuff);
	
	return 0;
}

