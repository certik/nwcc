#include <stdio.h>

/*
 * Test proper global symbol export
 *
 * This test establishes basic linking sanity between multiple
 * nwcc-compiled modules. It was written because the XLATE_IMMEDIATELY
 * redesign exposed a nasm bug that broke this sanity
 */
void		global_func0();	/* prints global_var0/1 */
extern void	global_func1();
void		global_func2(void);
extern void	global_func3(void);
/* func4 intentionally implicit as int! */
/*void	global_func4();*/

extern int	global_var0;
extern int	global_var1;

/* XXX tentative declarations are missing ... */

int
main() {
	global_func0();
	global_func1();
	global_func2();
	global_func3();
	(void) global_func4();

	global_var0 = 111;
	global_var1 = 4732;
	global_func0();
	return 0;
}

