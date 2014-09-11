void
foo(int x) {
}

void
baz(char *p, int val) {
}


struct o {
#if 0
	int	x;
	char	buf[128];
	short	foo;
#endif
	int	*p;
};

int
f(void) {
#if 0
	return 0;
#endif
}

int
main(void) {
#if 0
	char	buf[0];
	struct o	*hm;
	struct o ol = { 0 };
	static int x;
	struct o *hm;
	++hm;
	*hm;
	
	&(x);
	*hm;
	++hm;
#endif
#if 0 
	char	*hm;
	int		x;
	char	buf[128];
	++hm;
	++x;
	puts("hello world");
	&*hm;
	/*if (1) puts("hello world");*/
	baz("hehe", 0);
	putchar(0);
#endif
	char	*hm;
	static int x = 5 + 5 * 5;
	int	y;
	puts("horld");
	puts("horld");
	printf("hel world %d\n", x);
	if (x == 5) {
		puts("huh");
	} else {
		puts("hmz");
	}
#if 0
	++x;
#endif
	if (x == 1337)
		puts("huh222222222222");
	else
		puts("good! x != 31");
#if 0
	if (y) {
		puts("good, y ! =0");
	}
#endif
#if 0
	x = y = 0;
	else
		puts("hmz");
#endif
}

