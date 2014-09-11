int
main(void) {
#if 0
	struct lol {
		int	x;
	};
#endif
	char	stuff[5][128];
#if 0 
	struct lol l;
	struct lol *p;
	p = &l;
	p->x = 1337;
	printf("%d\n", p->x);
#endif
	stuff[3][2] = 69;
	printf("%d\n", stuff[3][2]);
}


