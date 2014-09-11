#include <stdio.h>

int
main() {
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	extern void	*(*bs(void))[1];
	puts((*bs())[0]);
}

void *
(*bs(void))[1] {
	static void	*txt[] = { "hello" };
	return &txt;
}

