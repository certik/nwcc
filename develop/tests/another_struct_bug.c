#include <stdio.h>

struct baz {
	char	x;
	char	y;
	union {
		char	*foo;
		int	hm;
	} stuff;	
};	
		
struct bar {
	struct baz	x;
	char		y;
	char		z[1];
};	

struct foo {
	char		*name;
	struct bar	nested;
	char		buf[16];
	char		ch;
};	


/*
 * what I'd expect on x86:
 *
 * 1.    4 bytes name  (dd)
 * 2.    1    byte x struct member   (db)
 * 3.    1    byte y struct member   (db)
 * 4.    2    padding bytes for alignment
 * 5.    4 bytes stuff union member
 * 6.    16 bytes for buf
 * 7.    1 byte for ch
 * 8.    3 bytes padding to meet alignment
 *    
 * what hapens:
 *
 * 1.    check
 * 2.    ???? 
 * 3.    check
 * ............. to be continued
 */

struct foo	f[] = {
	{ "first", {{ 1 }}, { 0 }, 1 },
	{ "second", {{ 2 }} , { 1 }, 2 },
	{ "third", {{ 3 }} , { 2 }, 3 },
};

int
main() {
	int	i;
	for (i = 0; i < 3; ++i) {
		printf("%d:   %s\n", i, f[i].name);
	}	
}	
