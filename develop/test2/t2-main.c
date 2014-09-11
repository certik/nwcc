#include "t2.h"
#include <stdio.h>


int
main() {
	struct smallfp	gfp = { 4.1166 };
	struct smallint	gi = { 7773 };
	struct smallmixed gm = { 4542, 0.631, 47733 };

	puts("   passing values...");
	getsmallfp(gfp);
	getsmallint(gi);
	getsmallmixed(gm);

	puts("   returning values...");
	gfp = retsmallfp(0.1666);
	gi = retsmallint(1337);
	gm = retsmallmixed(777, 888.88f, 999);
	printf("%f\n", gfp.f);
	printf("%d\n", gi.i);
	printf("%d, %f, %d\n", gm.i, gm.f, gm.i2);
}

	
