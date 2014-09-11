#include <stdio.h>
#include <stdarg.h>

int
foo() {
	return puts("hello");
}

static void bogus(int);

extern void
bogus(int x) {
	printf("%d\n", x);
}

static void bogus(int);

static int lol = 7;
extern int lol;

void varfunc(char *fmt, ...)
{
	va_list	va;
	va_list	*pva = &va;
	va_list	**ppva = &pva;
	void	*p;

	va_start(va, fmt);
	p = ( 1? __builtin_va_arg(va, void *): 0);
}

typedef char	*tdef;

int
main() {
	void		(*stuff[1])();
	char		buf[10];
	char		*cp = buf;
	unsigned short	us[] = { 'h', 'e', 'l', 'l', 'o', 0 };
	unsigned short	*usp = us;
	int		stuffint = {  3233, };


	stuff[0] = (void(*)())foo;
	printf("%d\n", ((int(*)())stuff[0])());
	for (usp = us; *usp != 0;) {
		*cp++ = *usp++;
	}
	*cp = 0;
	puts(buf);
	printf("%d, %d\n", stuffint, lol);
	bogus(128);
	{
		char	*p = "hello";
		char	**tdef = &p;
		(tdef)[0];
		printf("%d\n", (int)strlen((tdef[0]))); 
	}

	varfunc(NULL, 128);
}

