extern int	printf(const char *, ...);

/* Brought up by Harald van Dijk */
int
main() {
	int (*f)(const char *msg);

	puts("hello");
	printf("%d\n", (int)sizeof &puts);
	(f = puts)("world");
	return 0;
}


