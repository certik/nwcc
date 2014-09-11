typedef int trash;

/*
 * 04/20/08: gcc allows the redeclaration only if the preprocessor
 * indicates that it was done in a different header file!
 * RIDICULOUS
 */
# 20 "/usr/include/stdio.h" 3 4
typedef int trash;

int
main() {
}

