int
main(void) {
	struct {
		int	x;
	} *p, foo = {
		0
	};

	p = &foo;

	++p->x;
	printf("%d\n", foo.x);
	return 0;
}

