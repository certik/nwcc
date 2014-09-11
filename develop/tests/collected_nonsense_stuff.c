#include <stdio.h>

/*
 * This stuff is supposed to pass the first three args in EAX, EDX, ECX,
 * and to let the callee deallocate any stack used for arguments (stdcall)
 *
 * nwcc does not support this yet, but regex uses it, but regex only
 * seems to use it for static functions
 */
void __attribute__((regparm(3), stdcall))
foo(int x, int y, int z, int yoctix)
{
	printf("%d\n", x, y, z,  yoctix);
}

void *
__attribute__((pure))   /* weird placement of attribute - regex again */
nonsense_stuff() {
}	

struct foo {
	int	x;
	int	y;
	int	z;
};

struct foo
structfunc() {
	struct foo	f = { 1, 2, 3 };
	return f;
}	

int
main() {
	int		before_foos = 5;
	struct foo	foos;
	int		after_foos = 4;
	struct xterm_nonsense {
		int	x;
		int	y;
		int	z;
	} xt, *xtp;

	xtp = &xt;
	xtp->z = 123;
	xtp->x = xtp->y = xtp->z;
	printf("%d %d %d\n", xtp->x, xtp->y, xtp->z);
		

	/*
	 * Some more xterm nonsense... it casts a static address initializer
	 * to a ``long''. gcc accepts this without warning by default but
	 * at least errors when casting to smaller types
	 */

	{
		struct bogus {
			long	val;
		};	
		static char	*addr;
		static struct bogus b = { (long)&addr };

		/*
		 * 08/18/07: This wasn't working for array initializers
		 * because the type handling is kludged (and rightly so,
		 * since it is such a complteely bogus thing to do.)
		 */
		static char	buf[128] = "hello";
		static struct bogus superbogus = { (long)buf };
		   
		if (&addr == (void *)b.val) {
			puts("good");
		} else {
			puts("bad");
		}
		puts((const char *)superbogus.val);
	}

	/*
	 * The stuff below is to test 08/18/07's change of where
	 * ignored struct returns go; Now the throw-away buffer
	 * is created when the stack frame is created, and not
	 * at the time of the call
	 */
	structfunc();
	printf("%d, %d\n", before_foos, after_foos); 
	return 0;
}

