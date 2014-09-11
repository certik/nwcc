int	hmm = 0;

int
main() {
	void	foo();
	foo();
}


void
foo() {
	int	puts(const char *);
	extern int hmm; /* texinfo does this :/ */
	puts("hello");
	printf("%d\n", hmm);
}

