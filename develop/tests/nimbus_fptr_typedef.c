typedef void	f();


f	*p, *p2;


void
func0() {
	puts("hello");
}

void
func1() {
	puts("world");
}	

int
main() {
	p = func0;
	p2 = func1;
	p();
	p2();
}

