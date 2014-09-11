#include <stdio.h>

static const char	*str = "Hello";
const char		*str2 = "world";

int
func() {
	printf("%s %s\n", str, str2);
}

