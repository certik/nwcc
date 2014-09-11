#include <stdio.h>


int
main() {
#define HM(x, y) \
	((x) > (y)? (x): (x) == (y)? 128: (y))

	printf("%f\n",
		(float)(123.43f * 3224l / 474.22
		+ (-3252.2L)
		/ (44.22*2)));
	printf("%f\n",
		(float)HM(123.422, 226.55f));	
	printf("%f\n",
		(double)HM(444.0L, 226.55));	
	printf("%f\n",
		(double)HM(123.422, 123.422));	
	return 0;
}	

