#include <stdio.h>

int
main() {
	int	inline;
	char	*asm = "hello";
	int	restrict = 1;

	inline = puts(asm+restrict);
	printf("%d\n", inline);
#if __GNUC__
	/* Ensure that this still works */
	__asm__("nop");
#endif
}

