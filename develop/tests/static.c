#include <stdio.h>

static int
foo(void) {
	static int	counter;
	return counter++;
}

static int
unixunixunix(int pluswhat) {
	static struct {
		char	unused[sizeof(int)];
		int	val;
	} blah;	
#if 0
	static struct {
		char	unused[sizeof(int)];
		int	val;
	} blah = {
		{0}, 0
	};
#endif
	blah.val = blah.val + pluswhat; 
	return blah.val;
}	
		

int
main(void) {
	int	i;

	for (i = 0; i < 5; ++i) {
		printf("%d\n", foo());
	}
	for (i = 0; i < 5; ++i) {
		printf("%d\n", unixunixunix(i));
	}	
	return 0;
}	

