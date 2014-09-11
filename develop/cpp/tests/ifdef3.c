int
main() {
#define TRUTH ident

#if TRUTH
        puts("false");
#endif

#define ident 1

#if TRUTH
        puts("true");
#endif
}
