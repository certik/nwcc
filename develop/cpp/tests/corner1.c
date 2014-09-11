#define foo(x) x * x
#define bar foo

int
main() {
	printf("%d\n", bar(123));
}	

