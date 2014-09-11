#include <stdio.h>


int
main() {
	double	foo = 123.422;
	double	bar = 331.42;

	if (foo > bar) {
		puts("bug");
	} else {
		puts("good");
	}
	if (foo == bar) {
		puts("buggg");
	}
	if (bar <= 331.42) {
		puts("good");
	} else {
		puts("bug");
	}
	if (bar / foo > 2) {
		puts("good");
	} else {
		puts("bug");
	}	

	foo = 1.43;
	bar = 1.43;

	if (foo > bar) puts("bug");
	else puts("good");
	
	if (foo >= bar) puts("good"); 
	else puts("bug");
	foo += 1.0;
	if (foo >= bar) puts("good"); 
	else puts("bug");
	foo -= 2.0;
	if (foo >= bar) puts("bug"); 
	else puts("good");

	foo = 1.43;
	bar = 1.43;

	if (foo < bar) puts("bug");
	else puts("good");

	if (foo <= bar) puts("good"); 
	else puts("bug");
	foo -= 1.0;
	if (foo <= bar) puts("good"); 
	else puts("bug");
	foo += 2.0;
	if (foo <= bar) puts("bug"); 
	else puts("good");

	return 0;
}	

