#define lol do {			puts("ke ke ke")  ; } while (0)
#define funclike(x, y) (x) * (y)
#define funclike2(x) #x

int
foo() {
	return 128;
}

int main(){
		lol;
		
		printf("%d\n", funclike(1+2, (4 - 32) * foo()));
		printf("%s\n", funclike2('c'));
}

