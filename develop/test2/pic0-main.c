#include <stdio.h>

extern int	lib_var;

void	lib_func0();
void	lib_func1(int val);
void	lib_func2();


int
main() {
int x;
lib_var = 123;
x = bogus_stuff();
printf("%d\n", x);
	lib_func0();
	lib_func1(123);
	lib_var = 456;
	lib_func2();
}

