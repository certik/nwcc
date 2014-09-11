#include <stdio.h>


union u {
	long long dummy;
	int i;
};

struct s {
	union u	m[2];
};


union u2 {
	int 	i;
};

struct s2 {
	union u2	m[2];
};

int
main() {
	struct s 	s = {{ { .i = 999 }, { .i = 333} }};
	struct s2	s2 = { { { .i = 333 }, { .i = 555 } } };
	struct s2	s3 = { { { .i = 123, .i = 454, .i = 323 }, { .i = 555 } } };
	union u {
		int	x;
		int	y;
		int	z;
	} bs = {
		.y = 123, .y = 456, .y = 789
	};

	printf("%d, %d\n", s.m[0].i, s.m[1].i);
	printf("%d, %d\n", s2.m[0].i, s2.m[1].i);
	printf("%d, %d\n", s3.m[0].i, s3.m[1].i);
	printf("%d\n", bs.y);

	return 0;
}

