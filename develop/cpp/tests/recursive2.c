#define x y
#define y x

#define draw(x, y, z) do { \
	char	buf[2]; \
	*buf = x; \
	y ## z(1, buf, 1); \
} while (0)	

int
main() {
	int	x = 'x';
	int	y = 'y';

	printf("%d\n", x);
	printf("%d\n", y);
	draw('!', wri, te);
	draw('\n', wri, te);
}

