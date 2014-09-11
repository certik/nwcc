
#define Lol

int
main() {
#ifdef Lol
	puts("Lol is defined!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
#else
	puts("is NOT defined!!1!11!");
#endif
#undef Lol
#ifdef Lol
	puts("is STLL defined?!?!");
#endif

#ifndef Lol
	puts("good! not defined!");
#else
	puts("huh...");
#endif
#define foo
#ifndef foo
	puts("wut");
#else
	puts("good");
#endif
}

