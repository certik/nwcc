#include <stdio.h>

void
foo(restrict, inline)
	char	*restrict;
	int	inline;
{
	printf("%s\n", restrict+inline);
}

int
main() {
	int	inline = 123;
	int	restrict = 56;
	printf("%d\n", inline+restrict);
	foo("hello", 2);
}

