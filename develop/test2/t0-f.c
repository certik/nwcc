#include <stdio.h>

void foo
(
float f0,
float f1,
float f2,
float f3,
float f4,
float f5,
float f6,
float f7,
float f8,
float f9,
float f10,
float f11,
float f12,
float f13,
float f14
)
{
printf("f0 = %f\n", f0);
printf("f13 = %f\n", f13);
}

void
foo2(
   float f0, float f1, float f2, float f3, float f4, float f5, float f6, float f7,
   float f8, float f9, float f10, float f11, float f12, float f13, float f14,
   float f15, float f16, float f17, float f18, float f19, float f20, float f21,
   float f22, float f23, float f24, float f25, float f26, float f27, float f28,
   float f29, float f30, float f31, float f32, float f33, float f34, float f35,
   float f36, float f37, float f38)
{
	puts("   2nd test (many fp stack args)");
#if 0
	printf("%f\n", f7);
	    printf("  -- %f\n", f8);
	    printf("  -- %f\n", f9);
	    printf("  -- %f\n", f10);
	    printf("  -- %f\n", f11);
	    printf("  -- %f\n", f12);
	    printf("  -- %f\n", f13);
	    printf("  -- %f\n", f14);
	printf("%f\n", f15);
#endif
	printf("%f\n", f16);
#if 0
	printf("%f\n", f23);
	printf("%f\n", f31);
	printf("%f\n", f37);
	printf("%f\n", f38);
#endif
}

