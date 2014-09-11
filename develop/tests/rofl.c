int
main(void) {
	int		lol = 128;
	int		x = 2;
#if 0
	unsigned char	c;

	c = 5;
	c = lol / x;
	printf("%d %d %d\n", lol, x, c);
	printf("%d\n", lol/x);
#endif
	printf("%d\n", lol^x);
	return 0;
}

