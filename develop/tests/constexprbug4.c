#include <stdio.h>

int
main() {
  struct bogus {
	  char	*p1;
	  char	*p2;
  } b; 
  b.p1 = (char *)0x1000000;
  b.p2 = (char *)0x0f4f4f4;
 
  /*
   * 07/15/08: The conditional operator yielded an incorrect (integral) result
   * type, so we had "(ptr - int) & int" instead of the correct "(ptr - ptr) &
   * int"
   */
 printf("%d\n", (int) ( ((b.p1) - (1 ? b.p2 : 0)) & 123) );
 return 0;
}

