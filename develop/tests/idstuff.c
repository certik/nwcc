int
main(void) {
	int	x = 5;
	int	*p = &x;
	++*p;
	printf("%d\n", x);
	(*p)--;
	printf("%d\n", x);
	return 0;
}

