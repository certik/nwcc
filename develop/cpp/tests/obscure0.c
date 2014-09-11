#define foo() "lol"
#define bar ()

static char *
(foo)() {
	return "hello";
}	

int
main() {
	/* this yields "hello", not "lol", with GNU cpp and ucpp */
	puts(foo bar);
}

