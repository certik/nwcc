#include <stdio.h>
#include <limits.h>

/*
 * This testcase mostly tests mixing of different sized arguments
 * It only uses constants as arguments
 */

/*
 * Test mixing of fp arguments (this is useful for multi-fpr architectures)
 */
void
test0(float a0, double a1, long double a2, float a3, float a4, long double a5, double a6, float a7, double a8, long double a9) {
	puts("test2");  /* flush registers/stack */
	printf("%f %f %Lf %f %f %Lf %f %f %f %Lf\n",
		 a0, a1, a2, a3, a4, a5, a6, a7, a8 , a9); 
}

/*
 * Mix fp with long and medium integers
*/
void
test1(float a0, long long a1, unsigned long a2, float a3, long double a4, int a5, double a6, long double a7, long long a8) {
	puts("test1");
	printf("%f %lld %lu %f %Lf %d %f %Lf %lld\n",
		a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

/*
 * Mix short integers
 */
void
test2(short a0, long long a1, signed char a2, int a3, long a4, long long a5, signed char a6, short a7, int a8, short a9, unsigned char a10) {
	puts("test2");
	printf("%d %lld %d %d %ld %lld %d %d %d %d %d\n",
		a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

/*
 * Mix arithmetic 
 */
void
test3(long double a0, signed char a1, double a2, float a3, long long a4, unsigned short a5, double a6, long double a7, short a8, float a9, float a10) {
	puts("test3");
	printf("%Lf %d %f %f %lld %u %f %Lf %d %f %f\n",
		a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}


/*
 * Throw in structs and unions. Note that we are testing for internal consistency
 * here, not system ABI compatibility.
 */
struct foo {
	char	*p;
	char	x;
	float	f;
	char	alignme[77];
};

struct bar {
	char	c;
	char	*p;
};

void
test4(int x, struct foo f, void *y, short s, long long ll, int i, short s2, short s3, short s4, signed char sc, struct foo f2, struct foo f3,
	struct bar b, struct foo f4, char *end) { 
	puts("test4");
	printf("%d %d %d %lld %d %d %d %d %d\n", x, *(char *)y, s, ll, i, s2, s3, s4, sc);
	printf("%s %d %f %s\n", f.p, f.x, f.f, f.alignme);
	printf("%s %d %f %s\n", f2.p, f2.x, f2.f, f2.alignme);
	printf("%s %d %f %s\n", f3.p, f3.x, f3.f, f3.alignme);
	printf("%c\n", b.c, b.p);
	printf("%s %d %f %s\n", f4.p, f4.x, f4.f, f4.alignme);
	puts(end);
}

int
main() {
	struct foo	f, f2, f3, f4;
	struct bar	b;

	test0(1.1f, 2.2, 3.3L, 4.4f, 5.5f, 6.6L, 7.7, 8.8f, 9.9, 10.1L);
	test1(1.1f, INT_MAX * 2LL, 3lu, 4.4f, 5.5L, 6, 7.7, 8.8L, INT_MAX + 3522LL);
	test2(SHRT_MAX, INT_MAX + 222222ll, SCHAR_MAX, 4, LONG_MAX, 353522222234222ll, SCHAR_MAX - 3, SHRT_MAX / 2, 333, 2222, 111);
	test3(1111.3522L, -32, 43444.22222, 123.36f, 22525225663333, 1555, 555.225, 7777.22222L, 124, 533.24f, 588.2f);

	f.p = "f0";
	f.x = 1;
	f.f = 1.1;
	strcpy(f.alignme, "\thello world");

	f2.p = "f1";
	f2.x = 2;
	f2.f = 2.2;
	strcpy(f2.alignme, "\tit is a good day");

	f3.p = "f2";
	f3.x = 3;
	f3.f = 3.3;
	strcpy(f3.alignme, "\tto get some");

	f4.p = "f3";
	f4.x = 4;
	f4.f = 4.4;
	strcpy(f4.alignme, "\ttesting and debugging done");

	b.c = 'x';
	b.p = "hmmm lol";
	test4(1, f, &f2.x, 3, 44444444443333ll, 5, 3263, 2255, 442, -32, f2, f3, b, f4, "the end");
	return 0;
}

