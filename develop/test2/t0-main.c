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
);

void
foo2(
   float f0, float f1, float f2, float f3, float f4, float f5, float f6, float f7,
   float f8, float f9, float f10, float f11, float f12, float f13, float f14,
   float f15, float f16, float f17, float f18, float f19, float f20, float f21,
   float f22, float f23, float f24, float f25, float f26, float f27, float f28,
   float f29, float f30, float f31, float f32, float f33, float f34, float f35,
   float f36, float f37, float f38);

int
main(
)
{
	foo(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
	foo2(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    		16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    		32,33,34,35,36,37,38,39);
}

