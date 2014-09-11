#include <stdio.h>

int
main() {
	long long foo = 123;
	long long bar;

	do {
		puts("1");
	} while ((foo /= 100ll) != 0);

	foo = 123;
	do {
		puts("2");
	} while (foo /= 100ll);

	foo = 123;
	do {
		puts("3");
	} while ((foo /= 10) == 12);

	foo = 123;
	do {
		puts("4");
	} while ((foo *= 10) != 12300);

	foo = 123;
	do {
		puts("5");
	} while (++foo < 125);

	foo = 123;
	do {
		puts("6");
	} while (++foo <= 12);
	
	foo = 123ll;
	bar = 128ll;

	printf("%d\n", foo < bar);
	while (1 && foo < bar) {
		printf("%lld %lld\n", foo, bar);
		++foo;
	}

	return 0;
}	
		
