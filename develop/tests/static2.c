int
main() {
	static int	i0 = 1 && 1;
	static int	i1 = 1 && 0;
	static int	i2 = 1 || 1;
	static int	i3 = 0 || 1;
	static int	i4 = 0 || 0;

	if (i0)
		puts("good");
	else	
		puts("bad");

	if (i1)
		puts("bad");
	else	
		puts("good");
		
	if (i2)
		puts("good");
	else
		puts("bad");

	if (i3)
		puts("good");
	else
		puts("bad");

	if (i4)
		puts("Bad");
	else
		puts("good");
}

