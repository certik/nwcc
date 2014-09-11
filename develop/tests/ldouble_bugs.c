void
foo(long double d) {
	struct bogus { long double b; } b = { d };
	printf("%Lf\n", b.b);
}

int
main() {
	foo(123.456L);
}

