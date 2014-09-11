#include <stdio.h>


/*
 * 08/06/09: Test stupid x86 gcc alignment rules: A long long is generally
 * 8-byte-aligned, but ISN'T as part of a struct!
 * Hope there aren't too many bugs because of this now...
 */
int
main() {
	struct foo {
		char	dummy;
		long long x;
	} f = {
		12, 456363LL
	};
	union bar {
		char	dummy;
		long long x;
	} b;

	b.x = 23423LL;


	printf("%d\n", __alignof(long long));
	printf("%d\n", __alignof(struct foo));
	printf("%d\n", __alignof(union bar));
	printf("%u\n", (unsigned)((char *)&f.x - (char *)&f));
	printf("%u\n", (unsigned)((char *)&b.x - (char *)&b));

	printf("%d %lld\n", f.dummy, f.x);
	printf("%d %lld\n", b.dummy, b.x);
	return 0;
}

