struct x { 
	unsigned x1:1;
	unsigned x2:2;
	unsigned x3:3;
};
   
int
main() {
	struct x b = {1, 2, 3};

	printf("%d %d %d\n", b.x1, b.x2, b.x3);
	printf("%d\n", b.x3);
	sync();
	b.x3 += 2;
	sync();
	printf("%d\n", b.x3);
}


