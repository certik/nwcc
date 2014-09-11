int
main() {
#if 16 - 8 == 8
	puts("good");
#elif 16 - 8 == 8
	puts("bad");
#else
	puts("very bad");
#endif

#ifdef Kekek
	puts("bad");
#elif 6666 - 1 > 6666
	puts("bad");
#else
	puts("ok");
#endif

#if 0
	puts("bad");
#elif 0
	puts("bad");
#elif 1
	puts("particularly good");
#elif 0
	puts("bad");
#elif 1
	puts("bad");
#else
	puts("bad");
#endif	


}

