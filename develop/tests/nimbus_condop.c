void	*foo(void **arg);
void	*bar();

int
main() {
	return 0;
}	

void *
foo(void **arg) {
	return *arg? (*arg = bar()): arg;
}

void *
bar() {
	return (void *)0x1;
}

