int
main() {
#if defined foo + 1 == 1
	puts("ha ha yes its 1");
#endif

#define foo

#if defined foo + 1 == 1
	puts("ha ha yes its 1");
#else
	puts("no sir it is not 1 (this is good)");
#endif

#if 87 - 2 > 661111
	puts("huh");
#elif defined(foo)
	puts("YES DEFINED!!");
#else
	puts("bad");
#endif
}	

