int
main(void) {
	char	buf[128];
	char	*p = buf;
	int	x = 101;
	int	ch;
#if 0

	printf("%d\n", p - buf);
	++p;
	printf("%p\n", p - buf);
	--p;
	printf("%p\n", p - buf);
	p += 16;
	printf("%p\n", p - buf);
#endif
	printf("%d\n", x * 20);

	printf("%d\n", x >> 2);

#if 0
	while ((ch = getchar()) != -1) {
		if (ch == 'x') {
			break;
		} else {
			continue;
		}	
	}
	switch (6) {
		default:
			puts("hmm");
	}		
	puts("mkay");
#endif
}	

