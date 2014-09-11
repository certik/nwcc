int
main(void) {
	char	*pointers[3];
	int		i;
/*#define NULL ((void *)0)*/
#define NULL 0

	pointers[0] = "hello";
	pointers[1] = "world";
	pointers[2] = NULL;

	for (i = 0; pointers[i] != NULL; ++i) {
		puts(pointers[i]);
	}
	return 0;
}

