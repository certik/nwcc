#include <stdio.h>
#include <string.h>


static void
dump(void *s, size_t size) {
	size_t		i;
	unsigned char	*ucp = s;

	for (i = 0; i < size; ++i) {
		printf("%02x ", *ucp++);
	}
	putchar('\n');
}


int
main(void) {
	/*
	 * 06/08/11: The purpose of this test is to demonstrate where (at which byte)
	 * the storge unit ends. If the unit ends after the member preceding "b", "b"
	 * will be stored in the next byte rather than putting some of its bits in
	 * the current partial byte
	 */
	struct gnu {
		int	a:4;
		int	b:7;
	} g;
	struct gnu2 {
		int	a:7;
		int	b:7;
	} g2;
	struct gnu3 {
		int	a:15;
		int	b:7;
	} g3;
	struct gnu4 {
		int	a:23;
		int	b:7;
	} g4;
	struct gnu5 {
		int	a:31;
		int	b:7;
	} g5;
	struct gnu6 {
		int	a:31;
		int	z:8;
		int	b:7;
	} g6;
	struct gnu7 {
		int	a:31;
		int	z:8;
		int	z2:8;
		int	b:7;
	} g7;

#define DOIT(s) do { \
	memset(&s, 0, sizeof s); \
	s.b = 0x7f; \
	dump(&s, sizeof s); \
} while (0)

	DOIT(g); DOIT(g2); DOIT(g3); DOIT(g4); DOIT(g5); DOIT(g6); DOIT(g7);
}

