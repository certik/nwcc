#include <stdio.h>

int	foo = 456;

int
main() {
	int	foo = 123;
	{
		extern int	foo;
		void		func();
		extern void	func2();

		printf("%d\n", foo);
		func();
		func2();
	}
	printf("%d\n", foo);
	return 0;
}

extern void
func() {
	puts("func");
}

void
func2() {
	puts("func2");
}	

