int
main(void) {
	int	i;

	for (i = 0; i < 5; ++i) {
		if (i % 2 == 0) {
			continue;
		}
		printf("%d\n", i);
	}

	i = 0;
	do {
		if (i % 2 == 0) {
			continue;
		}
		printf("%d\n", i);
	} while (++i != 6);	

	return 0;
}

