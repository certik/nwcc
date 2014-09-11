#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void
vasprintf_test(char **buf, const char *fmt, va_list va) {
	char	*b;
	const char	*p;
	va_list	temp;
	int	len = 0;

	sync();
	va_copy(temp, va);
	sync();
	for (p = fmt; *p != 0; ++p) {
		if (*p == '%') {
			len += strlen(va_arg(temp, char *));
			++p;
		} else {
			++len;
		}
	}


	b = malloc(len + 1);
	*buf = b;
	vsprintf(b, fmt, va);
}

void
asprintf_test(char **buf, const char *fmt, ...) {
	va_list	va;

	va_start(va, fmt);
	vasprintf_test(buf, fmt, va);
	va_end(va);
}

int
main() {
	char	*p;
	asprintf_test(&p, "hello %s, %s", "world", "hhmhmhmhhhmmhmhmhhmhmmhmhmm");
	puts(p);
}	

