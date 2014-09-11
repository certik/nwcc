#include <stdio.h>


struct s {
	char	dummy;
	char	dummy2;
	char	dummy3;
} s_instance;

int
main() {
	static char	buf[128];
	static int	zero = buf - buf;
	static int	one = (char *)buf+1 - buf;
	/* XXX typing is too lax! it transforms to
	 * s_instance+offset and doesn't refer to dummy3 at all, and
	 * seems to use the same type for all
	 */
	static int	two = &s_instance.dummy3 - (char *)&s_instance;
/*	static int	nonsense = &zero - buf; ok - fails */
/*	static int	four = &s_instance.dummy3 > &s_instance.dummy? 4: -1;*/
	static int	five = &s_instance.dummy2 != &s_instance.dummy2? -1: 5;
	
	
	printf("%d\n", zero);
	printf("%d\n", one);
	printf("%d\n", two);
/*	printf("%d\n", four);*/
	printf("%d\n", five);
}
