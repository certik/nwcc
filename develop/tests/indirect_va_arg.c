#include <stdarg.h>

void
vfoo(const char *fmt, va_list *nonsense) {
	char	*p;

	while (*fmt) {
		if (*fmt == '%') {
			++fmt;
			puts(va_arg(*nonsense, char *));
		}
		++fmt;
	}
}

void
foo(const char *fmt, ...) {
	va_list	va;
	va_start(va, fmt);
	vfoo(fmt, &va);
}

int
main() {
	foo("%s%s", "hello world", "nonsnese sux");
}

