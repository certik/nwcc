int
main(void) {
	char	buf[128];
	int	x;
	char	*p;
	union {
		char	big[999];
		float	lol;
	} u;
	struct bogus {
		int	i;
		char	c;
	};
		

	printf("sizeof(char[128]) = %lu\n", (unsigned long)sizeof buf);
	printf("sizeof(buf + 1) = %lu\n", (unsigned long)sizeof(buf + 1));
	printf("sizeof(*p) = %lu\n", (unsigned long)sizeof *p);
	printf("sizeof(int) = %lu\n", (unsigned long)sizeof x);
	printf("sizeof(char *) = %lu\n", (unsigned long)sizeof p);
	printf("sizeof(union{char b[999];float lol}) = %lu\n",
		(unsigned long)sizeof u);
	printf("sizeof \"lolz\" = %lu\n", (unsigned long)sizeof "lolz");
	/*printf("sizeof *p += 5 = %lu\n", (unsigned long)sizeof (*p += 4));*/
	printf("sizeof (struct bogus) = %lu\n",
		(unsigned long)sizeof(struct bogus));	
	return 0;
}	


