int
main() {
	static char	buf[128];
	printf("%d\n", ("hello"? 123: 0));
	printf("%d\n", (!"hello"? 123: 0));
	printf("%d\n", (buf? 123: 0));
	printf("%d\n", (!buf? 123: 0));
}

