
extern long long	bogus_cond;
extern long long	bogus_and;
extern long long	bogus_or;

int
main() {
#ifdef __GNUC__
	/*
	 * More glibc nonsense; Instead of turning wrong constant expressions into compile-time
	 * errors, they depend on gcc optimizing variable access away in the correct case. And
	 * in the incorrect case, a linker error tells us that something went wrong
	 */
	int	i = 1? 123: bogus_cond;
	int	i2 = 0 && bogus_and;
	int	i3 =  1 || bogus_or;
	printf("%d %d\n", i2, i3);
	printf("%d,%d,%d\n", i, i2, i3);
	printf("%d,%d,%d\n",
		sizeof(1? 123: bogus_cond),
		sizeof(0 && bogus_and),
		sizeof(1 || bogus_or));
			
#else
	printf("%d %d\n", 0, 1);
	printf("%d,%d,%d\n", 123, 0, 1);
	printf("%d,%d,%d\n", (int)sizeof bogus_cond, (int)sizeof(int), (int)sizeof(int));
#endif
	return 0;
}

