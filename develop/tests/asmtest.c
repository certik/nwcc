/*
 * Examples taken from
 * http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html
 */

#include <stdio.h>


static inline char * asm_strcpy(char * dest,const char *src)
{
	int	foo;
__asm__ __volatile__(  "1:\tlodsb\n\t"
                       "stosb\n\t"
                       "testb %%al,%%al\n\t"
                       "jne 1b"
		       : "=S" (foo)
                     : "S" (src),"D" (dest)
                     : "memory");
return dest;
}
int main(void)
{
        int foo = 10, bar = 15;
	short	shrt = 127;
        char    msg[128];
        __asm__ __volatile__("addl  %%ebx,%%eax"
                             :"=a"(foo)
                             :"a"(foo), "b"(bar)
                             );
        asm_strcpy(msg, ".1: foo+bar=%d\n");
        printf(msg, foo);
#define byte_swap_word(x) \
        __extension__ ({ register short _x = x; \
                __asm("xchgb %h0, %b0": "+q" (_x)); \
                 _x;} )
        printf("%d\n", byte_swap_word(shrt));

	/*
	 * 08/03/07: __asm__ as expression statement wasn't parsed
	 * correctly!
	 */
	if (1)
		__asm__("nop");

        return 0;
}

