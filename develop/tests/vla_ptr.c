#include <stdio.h>
#include <string.h>

int
main() {
	/*
	 * Determine whether the compiler uses C89 or
	 * C99 type ranking for integer constants; On
	 * a system with 32bit int, 32bit long and
	 * 64bit long long, the constant is too large
	 * for int (INT_MAX+1) and must either be
	 * considered an "unsigned long" or "signed
	 * long long"
	 * In C89 mode, we want C89 typing.
	 */
	int	i = 16;
	char	buf[i];
	char	(*p)[i] = &buf;

	strcpy(*p, "nonsense");
	puts(buf);
}

