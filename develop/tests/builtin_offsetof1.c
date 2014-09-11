#include <stdio.h>

struct absurd {
	int	x;
	int	y;
	char	buf[128];
};

struct gnu {
	struct {
		struct absurd	a;
	} s;
};

union hurd {
	struct absurd	a;
};


int
main() {
	int		n = 123;
	static int	s1 =__builtin_offsetof(struct absurd, y);
	static int	s2 = __builtin_offsetof(struct absurd, buf /*[20]*/);
	int		s3 = __builtin_offsetof(struct gnu, s.a.buf[n]);

	printf("%d\n", __builtin_offsetof(struct absurd, y));
	printf("%d\n", __builtin_offsetof(struct absurd, buf[20]));
	printf("%d\n", __builtin_offsetof(struct gnu, s.a.buf[n]));
	printf("%d\n", s1);
	printf("%d\n", s2);
	printf("%d\n", s3);

	printf("%d\n", __builtin_offsetof(struct { int bs; int x; int y; }, y));
	printf("%d\n", (size_t)&((struct { int nonsense; char x; int n2;} *)0)->n2);

	printf("%d\n", __builtin_offsetof(union hurd, a.y));
	return 0;
}

