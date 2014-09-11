
typedef unsigned long long intmax_t;
int
main() { 
	unsigned x;
	int y;
	char	buf[(((sizeof (intmax_t) * 8 - (!((intmax_t)0 < (intmax_t)-1))) * 302 / 1000 + 1 + (!((intmax_t)0 < (intmax_t)-1))) + 1)];
	printf("%d\n", sizeof buf);

	y = -1;
	x = 10;
	if (x < y) puts("good");
}
