#include <stdio.h>

int
main() {
	struct f {
		int	x, y, z;
	} f=  {
		.x = 123,
		455,
		.z = 55,
		.x = 444,
		33,
		.z = 99
	};
	printf("%d,%d,%d\n", f.x,f.y,f.z);
		
}	
