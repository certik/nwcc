#define foo( x, y) x ##  y

int
main() {
	int	x = 1;
	int	y = 2;
	int	a = 3;
	int	b = 4;
	int	ab = -111111;
	
	foo( a, b ) = 8889;
	printf("%d\n", ab);
}	
