#define foo(x) #x

int
main() {
	puts(foo(hello + world "hehe"));
	puts(foo('"'));
}

