int
main() {
	int	one = 1;
	if ((one)) {
		puts("one");
	}

	if ((one) - (one)) {
		puts("bug");
	} else {
		puts("zero");
	}

	if (( (one) - (one))) {
		puts("bug");
	} else {
		puts("zero");
	}
}

