#define VAL 123
#define VAL2 VAL,VAL
#define VAL4() VAL2,VAL2

int
main() {
	printf("%d %d %d %d\n", VAL4());
}	

