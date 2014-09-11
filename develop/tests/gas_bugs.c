int
main() {
	struct foo {
		int	x;
		int	y;
	};

	static struct foo hm;

	int *p = &hm.y;  /* unbelievable, this didn't work with gas */
	*p = 123;
	printf("%d\n", hm.y);
}

