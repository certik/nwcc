#include <stdio.h>

void	foo();
void
foo(str, lol)
	const char	*str;
	int lol;
{
	puts(str);
}

extern void bar(char *bogus);

void
bar(bogus)
	char	*bogus;
{
	puts(bogus);
}

int
main() {
	foo("hello");
	bar("world");
}	

