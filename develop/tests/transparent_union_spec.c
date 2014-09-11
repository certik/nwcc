
typedef union {
	int	*ip;
	char	*cp;
} fooarg __attribute__((transparent_union));

void	foo(fooarg);

void
dummy() {
	foo((int *)0);
}


/*
 * 02/28/09: Check that transparent union argument specialization
 * works. This is e.g. needed for glibc's accept() declaration when
 * a program redeclares accept() itself (for example in a configure
 * script to determine the argument types)
 */
void 
foo(char *f) {
}


int
main() {
}


