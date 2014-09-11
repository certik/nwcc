typedef unsigned long size_t;

static size_t skip_fields;

int
main()
{
	void		*p = &skip_fields;
	static int	s = (char *)0xeeee - (char *)0x1111;
	static int	equ = (char *)0xc3c3c3 == (char *)0xc3c3c3;
	static int	equ2 = (char *)0xc3c3c0 == (char *)0xc3c3c3;
	static int	f0 = (char *)1 > 0;
	static int	f1 = (char *)1 < 0;
	static int	f2 = (char *)3 >= (char *)3;
	static int	f3 = (char *)3 <= (char *)4;
	static int	f4 = (char *)3 != (char *)4;
	static int	scale0 = (int *)60 - (int *)20;
	static int	scale1 = (long long *)20 - (long long *)60;
	static int	scale2 = (double *)10 - (double *)7;
	static int	scale3 = (double *)64 - (double *)32;

	if ((&skip_fields == (size_t *)0))
		puts("nonsense");
	else if ((&skip_fields == p))
		puts("ok");
	else
		puts("what?");

	if ((void *)0xffff == (void *)0xfefe) {
		puts("false");
	} else if ((void *)0xccc == (void *)0xccc) {
		puts("good");
	}

	printf("%d, %d, %d\n", s, equ, equ2);
	printf("%d, %d, %d, %d, %d\n", f0, f1, f2, f3, f4);
	printf("%d, %d, %d, %d\n", scale0, scale1, scale2, scale3);
	return 0;
}

