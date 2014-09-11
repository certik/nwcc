#include <stdio.h>

struct S {
	unsigned x:4;
	unsigned y:6;
} s;

struct S2 {
	unsigned long long	x:34;
} s2;

int
main() {
	s.y = 31;
	printf("%d\n", s.y);
	s.x = s.y;
	printf("%d\n", s.x);
	s.y = s.x;
	printf("%d\n", s.y);
	s.y = 0;
	s.x = 0;
	printf("%d\n", s.x);
	printf("%d\n", s.y);
	s.y += 31;
	s.x += s.y;
	printf("%d\n", s.x);
	printf("%d\n", s.y);

	s2.x = 0xffffffffffffffLLU; /* 52 bits */
	printf("%llx\n", s2.x);
	s2.x = 0;
	printf("%llx\n", s2.x);
	s2.x += 0xffffffffffffffLLU; /* 52 bits */
	printf("%llx\n", s2.x);

	return 0;
}

