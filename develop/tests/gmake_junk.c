#include <stdio.h>
#include <inttypes.h>

int
main() {
	unsigned long long	ull;
	signed long long	sll;

	printf("%d\n", sizeof(uintmax_t));
	printf("NEW_MTIME=%u\n",
	(unsigned)(~ (uintmax_t) 0 - (! ((uintmax_t) -1 < 0) ? (uintmax_t) 0 : ~
	(uintmax_t) 0 << (sizeof (uintmax_t) * 8 - 1))));
	
	printf("NEW_MTIME shifted=%u\n",
	(unsigned)((~ (uintmax_t) 0 - (! ((uintmax_t) -1 < 0) ? (uintmax_t)0 :
	~ (uintmax_t) 0 << (sizeof (uintmax_t) * 8 - 1))) >> 30));

	printf("MAX=%u\n",
	(((  ~ /* (uintmax_t)*/ 0 - 3) >> 30)  << 30   ) /* + 
	1000000002*/);

	ull = ~(uintmax_t)0;
	printf("-- unsigned chain --\n");
	printf("  %llu\n", ull);
	ull -= 3;
	printf("  %llu\n", ull);
	ull >>= 30;
	printf("  %llu\n", ull);
	ull <<= 30;
	printf("  %llu\n", ull);

	sll = ~0;
	printf("-- signed chain --\n");
	printf("  %lld\n", sll);
	sll -= 3;
	printf("  %lld\n", sll);
	sll >>= 30;
	printf("  %lld\n", sll);
	sll <<= 30;
	printf("  %lld\n", sll);

	printf("SIZEOF MTME=%u\n",
	sizeof ((~ (uintmax_t) 0 - (! ((uintmax_t) -1 < 0) ? (uintmax_t) 0 : ~
	 (uintmax_t) 0 << (sizeof (uintmax_t) * 8 - 1)))));
	return 0;
}
