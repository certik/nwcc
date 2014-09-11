int
main() {
#if 1 && 1
	puts("good");
#else
	puts("bad");
#endif

#if 1 && 0
	puts("bad");
#else
	puts("good");
#endif
		
#if 1 || 1
	puts("good");
#else
	puts("bad");
#endif

#if 0 || 1
	puts("good");
#else
	puts("bad");
#endif

#if 0 || 0
	puts("Bad");
#else
	puts("good");
#endif
}

