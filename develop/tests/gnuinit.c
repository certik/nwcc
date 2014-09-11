#include <stdio.h>

int
main() {
	union foo
	{
	     	unsigned __l /*__attribute__((__mode__(__SI__)))*/;
		float __d;
	};
	float __d = 
         (
	     ((union foo)
	    {
		__l: 0x7fc00000UL
   	   }).__d
	);
	printf("%f\n", __d);
}
