/*
 * This tests the select() macros FD_ZERO()/FD_SET() and FD_ISSET(). The
 * point of this exercise, is that some glibc systems (with __GNUC__ set
 * to 2 or higher) use nasty inline asm statements to perform these
 * operations. So it's primarily an inline asm test for such systems
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdio.h>

static void
dump_cksum(fd_set *set) {
	unsigned char	*ucp = (unsigned char *)set;
	int		i;
	int		bytes_per_cksum = sizeof(fd_set) / 10;

	if (bytes_per_cksum == 0) {
		puts("excuse me?!?!?!?");
		return;
	}

	/*
	 * The fd_set may be rather large, so we just print 10 checksums
	 */
	for (i = 0; i < 10; ++i) {
		int	sum = 0;
		int	j;

		for (j = 0; j < bytes_per_cksum; ++j) {
			sum = ((sum * 11) + *ucp++) & 0xffff;
		}
		printf("%04x ", sum);
	}
	putchar('\n');
}

int
main() {
	fd_set		set;
	unsigned char	*ucp;
	struct timeval	t;
	int		rc;

	t.tv_sec = 2;
	t.tv_usec = 0;

	memset(&set, 0x3a, sizeof set);
	dump_cksum(&set);
	FD_ZERO(&set);
	dump_cksum(&set);
	FD_SET(0, &set);
	dump_cksum(&set);
	FD_SET(126, &set);
	FD_SET(22, &set);
	FD_SET(5777, &set);
	dump_cksum(&set);

	rc = select(0 + 1, &set, NULL, NULL, &t);
	if (rc <= 0) {
		puts("bug");
	} else {
		char	buf[128];

		dump_cksum(&set);
		if (FD_ISSET(0, &set)) {
			rc = read(0, buf, sizeof buf - 1);
			if (rc <= 0) {
				puts("bug");
			} else {
				buf[rc] = 0;
				printf("%s\n", buf);
			}
		} else {
			puts("bug");
		}
	}
	return 0;
}

