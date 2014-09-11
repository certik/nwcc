#include <stdio.h>

int
breakme(int p0, int p1, void *p2, int p3, int p4, int p5) { 
#if 0
	printf("%d %d %d %d %d\n",
		p0, p1, p3, p4, p5);
	printf("%s\n", p2);
#endif
}


void *
get_str() {
	return "hello";
}

struct foo {
	int	width;
};

int 
main()
{
	int x = 123;

	/*
	 * 05/20/11: This yields an unbacked vreg error on AMD64 (with cross
	 * compilation too). The combination of function call, multiplication
	 * and many arguments triggers this (removing any of the first two
	 * args doesn't)
	 *
	 * (That was a multipliction preparation bug; The x86 preparation
	 * routine was used for AMD64, but always freed the 32bit regs (eax,
	 * edx) instead of rax/rdx
	 */
	breakme(1, 2, get_str(), 3, 4, 5 * x);
}

