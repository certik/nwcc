#include <stdio.h>


void
foo(void) {
	puts("foo sux");
}

void
bar() {
	puts("bar rulz");
}


void
doit(int i)
{
  void (*fp) () = i? foo: bar;
  (*fp)();
}


int
main() {
	doit(0);
	doit(1);
}
	
