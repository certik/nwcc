//#include <stdio.h>

struct xxx { 
	int	x;
};	

struct xxx 
func() {
	struct xxx	f;
	f.x = 444;
	return f;
}	

struct foo {
	int	x;
	char	*p;
	int	z;
};

struct foo	static_complit = (struct foo) { .p = "hmm", 4443, .x = 166 };
char		*p = (char[5]){ "xxx" };

int
main() {
	struct foo	*fp;
	int		x;
	int		i;

	static struct foo f = (struct foo) { 66, "hhh", 33 };

	printf("%d\n", (struct foo) {  123, "hello", 345 }.x);
	fp = &(struct foo){ 123, "Hello", 456 };
	printf("%d,%s,%d\n", fp->x, fp->p, fp->z);
	fp = &(struct foo){ .z = 345, .x = 3333, "hmmm" };
	printf("%d,%s,%d\n", fp->x, fp->p, fp->z);
	printf("%d\n", ((struct xxx) { 123 } = func()).x);

	printf("%d,%s,%d\n", static_complit.x, static_complit.p, static_complit.z);


	for (i = 0; i < sizeof "hello\n" - 1; ++i) {
		x = (char[]){ 'h', 'e', 'l', 'l', 'o', '\n', '\0' }[i];
/*		printf("%d\n", sizeof ((char[]){ 'x', 'y' }));*/
		putchar(x);
	}
	puts(p);
	printf("%d,%s,%d\n", f.x, f.p, f.z);
	return 0;
}	

