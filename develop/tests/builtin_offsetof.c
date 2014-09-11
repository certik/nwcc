#include <stdio.h>
#include <stddef.h>


int
main() {
	
	struct foo {
		char	x;
		int	y;
		long	z;
		void	*p;
		void	*hehe;
	};
#if defined __NWCC__ || (defined __GNUC__ && __GNUC__ > 4)
	static int	constoff = __builtin_offsetof(struct foo, p);
	printf("const = %d\n", constoff);
	printf("%d\n", (int)__builtin_offsetof(struct foo, x));
	printf("%d\n", (int)__builtin_offsetof(struct foo, y));
	printf("%d\n", (int)__builtin_offsetof(struct foo, z));
	printf("%d\n", (int)__builtin_offsetof(struct foo, p));
	printf("%d\n", (int)__builtin_offsetof(struct foo, hehe));
	printf("size = %d\n", (int)sizeof __builtin_offsetof(struct foo, x));
#else	
	static int	constoff = offsetof(struct foo, p);
	printf("const = %d\n", constoff);
	printf("%d\n", (int)offsetof(struct foo, x));
	printf("%d\n", (int)offsetof(struct foo, y));
	printf("%d\n", (int)offsetof(struct foo, z));
	printf("%d\n", (int)offsetof(struct foo, p));
	printf("%d\n", (int)offsetof(struct foo, hehe));
	printf("size = %d\n", (int)sizeof offsetof(struct foo, x));
	return 0;
#endif
}

