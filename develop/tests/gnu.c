#define HM(arg) ({ \
	int	x = arg; \
	int	i; \
	for (i = 0; i < x; ++i) { \
		printf("%d\n", ({1; 2; puts("hello"); i; })); \
	} \
	i; \
})	


int
main() {
	int	x;

	printf("%d\n", ({puts("lol");}));
	if (HM(5) == 5) {
		puts("good");
	}	
}
		
