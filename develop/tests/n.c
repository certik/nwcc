#if 0
int
main(void) {
	int	*p;
	int	x;
	p = &x;
	memset(&p, 0, sizeof p);
#if 0	
	/* WORX */
	int	z = getchar();
	int	x;
	x = (z / 2) * 2 - 1;
	printf("%d\n", x);
#endif
	printf("%p\n", p);

}
#endif

int
main(void) {
#if 0
	int	buf[128];
	buf[5] = 128;
	printf("%d\n", buf[5]);
#endif
#if 0
	char	*p;
	char	x;
	p = &x;
	*p = 0;
	WORKSS !!!!!!!!!!!!!!!!!!!!!!
#endif
#if 0
	int	*p;
	int	lol = 5;
	int	x;
	p = &lol;
	x = *p;
	printf("%d\n", x);
#endif
#if 0
	below works
#endif
	long	l, *l2, **l3, ***l4;
	l2 = &l;
	l3 = &l2;
	l4 = &l3;
	***l4 = 5;
	printf("%d\n", l);
	l2[0] = 10;
	printf("%d\n", ***l4);
#if 0
	printf("%d\n", l);
	printf("%d\n", ****l5);

#endif
#if 0
	int	*p;
	int	x;
	p = &x;
	*p = 1337;
	printf("%d\n", x);
#endif
}

