int
main(void) {
	long	arr[128];
	long	*p1;
	long	*p2;
	int		res;

	p1 = arr;
	p2 = arr + 10;
	printf("%d\n", p2 - p1);
	return 0;
}

