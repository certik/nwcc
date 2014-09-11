
	int	i;

int
func(void) {
	return 1337;
}

int
main(void) {
	struct foo {
		struct {
			int	x;
			int	y;
		} hehe;
	} bar;
	int	z;
	bar.hehe.x = 1337;
	bar.hehe.y = 2674;
	z = bar.hehe.x + bar.hehe.y;
	printf("%d\n", z);
	printf("%d\n", bar.hehe.x);

	z =  func();
	printf("the result of func is %d\n", z);

	for (i = 0; i < 5; ++i) {
		puts("hehe");
	}
#if 0
	struct foo {
		int	x;
	} bar;
	struct foo baz;
	int	z;

	z = bar.x;

#endif
#if 0
	i = 1 + (2 + 3);
	printf("%d\n", i);

	/* works */
	i = getchar() - 1;
	printf("%c\n", i);
#endif
	
#if 0
#define EOF (-1)
	for (i = 0; i < 5; ++i) {
		int ch;
		ch = getchar();
		if (ch == EOF) {
			puts("Whoops, EOF");
		} else if (ch == 'x') exit(0);
		else printf("%c\n", ch);
	}
#endif
}
