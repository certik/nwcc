void
foo(void) {
	extern char buf[128];

	{
		extern char buf[];
		if (sizeof buf == 128) {
			puts("OK");
		} else {
			puts("BUG");
		}
	}	
}

char	buf[128];

int
main() {
	foo();
}

