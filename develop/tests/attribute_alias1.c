void interface(void) __attribute__((alias("implementation")));

int
main() {
	interface();
}

void
implementation(void) {
	puts("hello. lol");
}

