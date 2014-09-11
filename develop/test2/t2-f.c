#include "t2.h"
#include <stdio.h>

void
getsmallfp(struct smallfp sf) {
	printf("%f\n", sf.f);
}

void
getsmallint(struct smallint si) {
	printf("%d\n", si.i);
}

void
getsmallmixed(struct smallmixed sm) {
	printf("%d, %f, %d\n", sm.i, sm.f, sm.i2);
}

struct smallfp
retsmallfp(float val) {
	struct smallfp	ret;	
	ret.f = val;
	return ret;
}

struct smallint
retsmallint(int val) {
	struct smallint	ret;
	ret.i = val;
	return ret;
}

struct smallmixed
retsmallmixed(int val0, float val1, int val2) {
	struct smallmixed	sm;
	sm.i = val0;
	sm.f = val1;
	sm.i2 = val2;
	return sm;
}
	
