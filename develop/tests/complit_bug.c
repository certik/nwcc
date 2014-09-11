#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

//int	filescope_status;

/*
 * The point of this test is primarily to test
 * __attribute__((__transparent_union__)); This is used on some GNU-
 * aware systems to declare the wait() function
 */
int
main() {
	static int	filescope_status;
	static int	i = 123;
	static int	*ptr = &(i);
	static int	*bs = ( /*(int *)&i,*/ (int *)&filescope_status);
	
	filescope_status = 0x05 << 8;

	printf("%d\n", 
		((
		(union { int __in; })
		{
			.__in = (filescope_status)
		}).__in));
	printf("%d\n", *ptr);
	printf("%d\n", *bs);
}



