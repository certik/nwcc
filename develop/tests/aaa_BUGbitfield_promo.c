#include <stdio.h>

int
main() {
	struct foo {
		unsigned int		x:10;
		unsigned long long	y:10;
		unsigned long long	z:33;

		unsigned int		uint:32;
	} f;

	/*
	 * The following checks always perform two checks per bitfield
	 * member:
	 *
	 *     - That the type is correctly signed or unsigned (this
	 * depends on whether or not it is promoted)
	 *
	 *     - That a usual arithmetic conversion for the + operator
	 * also promotes/cpnverts the result to a type of at least the
	 * correct size (if not sign and identity)
	 */
	f.x = 123;
	if (f.x < -1) {
		puts("BUSTED");
	} else {
		puts("OK");
	}
	printf("%d\n", (int)sizeof (f.x + 0));



	f.y = 123;
	if (f.y < -1) {
		puts("BUSTED");
	} else {
		puts("OK");
	}
	printf("%d\n", (int)sizeof (f.y + 0));



	f.z = 123;
	if (f.z < -1) {
		/*
		 * The type is big enough to not be promoted, so it
		 * remains unsigned long long, and should thus compare
		 * smaller to -1
		 */
		puts("OK");
	} else {
		puts("BUSTED");
	}
	printf("%d\n", (int)sizeof (f.z + 0));


	f.uint = 123;
	if (f.uint < -1) {
		/*
		 * The type is big enough to not be promoted, so it
		 * remains unsigned long long, and should thus compare
		 * smaller to -1
		 */
		puts("OK");
	} else {
		puts("BUSTED");
	}
	printf("%d\n", (int)sizeof (f.uint + 0));

	/*
	 * Now check that unary operators also yield an approximately
	 * correct result type
	 */
	printf("%d\n", (int)sizeof !f.x);  /* should be sizeof(int) for all */
	printf("%d\n", (int)sizeof !f.y); 
	printf("%d\n", (int)sizeof !f.z); 
	printf("%d\n", (int)sizeof !f.uint); 


	printf("%d\n", (int)sizeof ~f.x);  /* should be variable size */
	printf("%d\n", (int)sizeof ~f.y); 
	printf("%d\n", (int)sizeof ~f.z); 
	printf("%d\n", (int)sizeof ~f.uint); 


	printf("%d\n", (int)sizeof -f.x);  /* should be variable size */
	printf("%d\n", (int)sizeof -f.y); 
	printf("%d\n", (int)sizeof -f.z); 
	printf("%d\n", (int)sizeof -f.uint); 

	printf("%d\n", (int)sizeof +f.x);  /* should be variable size */
	printf("%d\n", (int)sizeof +f.y); 
	printf("%d\n", (int)sizeof +f.z); 
	printf("%d\n", (int)sizeof +f.uint); 


	/*
	 * Now actually perform those unary operations
	 */
	printf("%d\n", !f.x);  /* should be sizeof(int) for all */
	printf("%d\n", !f.y); 
	printf("%d\n", !f.z); 
	printf("%d\n", !f.uint); 


	printf("%d\n", ~f.x);  /* should be variable size */
	printf("%d\n", ~f.y); 
	printf("%lld\n", ~f.z); 
	printf("%u\n", ~f.uint); 


	printf("%d\n", -f.x);  /* should be variable size */
	printf("%d\n", -f.y); 

puts("===");
sync();
	/* XXX this gives unexpected results. why? */
	printf("%lld\n", -f.z); 
sync();
puts("===");

	printf("%u\n", -f.uint); 

	printf("%d\n", +f.x);  /* should be variable size */
	printf("%d\n", +f.y); 
	printf("%lld\n", +f.z); 
	printf("%u\n", +f.uint); 

	return 0;
}

