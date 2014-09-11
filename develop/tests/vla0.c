#include <stdio.h>


int
lol()  {
	char	*p;
	char	*start;

	for (p = start = "hello\n"; *p != 0; ++p) {
		putchar(*p);
	}
	return p - start;
}

int
lmao(){
	puts("owell");
	return 128;
}

int
main() {
	int	x = 4;
	int	siz;
	char	buf[lol()]  /*[lmao()]*/;
	char	*p;

	strcpy(buf, "hello");
	puts(buf);

	siz = sizeof buf;
	printf("vla size = %d\n", siz);
	strcpy(buf+2,"hm");
	puts(buf);

	p = buf;
	if (p == buf) {
		puts("yes");
	}
	p = buf + 1;
	if (--p == buf) {
		puts("yes");
	}
	buf[1] = 'Y';
	buf[0] = 'p';
	puts(buf);
	x = buf[0];
	buf[1] = x;
	puts(buf);
}

