int
main(void) {
	int	x;

	x = !0;
	printf("!0 is %d\n", x);
	x = !!0;
	printf("!!0 is %d\n", x);
	x = !!1;
	printf("!!1 is %d\n", x);
	x = !!127;
	printf("!!127 is %d\n", x);
	x = !(9 - 4);
	printf("!(9 - 4) is %d\n", x);
	{
		int	i = 99999;
		x = !!!!!i++;
		printf("!!!!!i++ (where i = 99999) is %d\n", x);
	}	
	return 0;
}

