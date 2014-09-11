#define foo() lol
#define bar(x) x()

int
main() {
	int	lol = 0;
	bar(foo) = 128;
	printf("%d\n", lol);
}

