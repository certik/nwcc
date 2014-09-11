#include <limits.h>
#include <stdio.h>

int
main(void) {
	signed char		schr = SCHAR_MAX - 123;
	unsigned char		uchr = UCHAR_MAX - 123;
	short			shrt = SHRT_MAX - 456;
	unsigned short		ushrt = USHRT_MAX - 666;
	int			i = INT_MAX - 3688;
	unsigned int		ui = UINT_MAX - 11122;
	long			l = LONG_MAX - 347;
	unsigned long		ul = ULONG_MAX - 90000;
	long long		ll = LONG_MAX - 4821;
	unsigned long long	ull = ULONG_MAX - 899;

	/* Convert to signed */
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)schr,
		(short)schr, (int)schr, (long)schr, (long long)schr); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)uchr,
		(short)uchr, (int)uchr, (long)uchr, (long long)uchr); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)shrt,
		(short)shrt, (int)shrt, (long)shrt, (long long)shrt); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)ushrt,
		(short)ushrt, (int)ushrt, (long)ushrt, (long long)ushrt); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)i,
		(short)i, (int)i, (long)i, (long long)i); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)ui,
		(short)ui, (int)ui, (long)ui, (long long)ui); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)l,
		(short)l, (int)l, (long)l, (long long)l); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)ul,
		(short)ul, (int)ul, (long)ul, (long long)ul); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)ll,
		(short)ll, (int)ll, (long)ll, (long long)ll); 
	printf("%d, %d, %d, %ld, %lld\n",
		(signed char)ull,
		(short)ull, (int)ull, (long)ull, (long long)ull); 

	/* Convert to unsigned */
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)schr,
		(unsigned short)schr, (unsigned int)schr, (unsigned long)schr,
		(unsigned long long)schr); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)uchr,
		(unsigned short)uchr, (unsigned int)uchr, (unsigned long)uchr,
		(unsigned long long)uchr); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)shrt,
		(unsigned short)shrt, (unsigned int)shrt, (unsigned long)shrt,
		(unsigned long long)shrt); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)ushrt,
		(unsigned short)ushrt, (unsigned int)ushrt, (unsigned long)ushrt,
		(unsigned long long)ushrt); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)i,
		(unsigned short)i, (unsigned int)i, (unsigned long)i,
		(unsigned long long)i); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)ui,
		(unsigned short)ui, (unsigned int)ui, (unsigned long)ui,
		(unsigned long long)ui); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)l,
		(unsigned short)l, (unsigned int)l, (unsigned long)l,
		(unsigned long long)l); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)ul,
		(unsigned short)ul, (unsigned int)ul, (unsigned long)ul,
		(unsigned long long)ul); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)ll,
		(unsigned short)ll, (unsigned int)ll, (unsigned long)ll,
		(unsigned long long)ll); 
	printf("%u, %u, %u, %lu, %llu\n",
		(unsigned char)ull,
		(unsigned short)ull, (unsigned int)ull, (unsigned long)ull,
		(unsigned long long)ull); 
	return 0;
}

