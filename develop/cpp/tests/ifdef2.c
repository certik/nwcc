#define lol

int
main() {
#if 0
#  if 1
	puts("very bad");
#  endif
#endif

#if 1
#  ifdef lol
	puts("good");
#  else
	puts("bad");
#  endif
	puts("hmm great");
#elif 1
	puts("bad bad bad");
#endif

}

