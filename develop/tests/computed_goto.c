#include <stdio.h>

int
main() {
	int	i;
	void	*labels[] = {
		&&lab3, &&lab2, &&lab1, &&lab0
	};

	for (i = 0; i < sizeof labels / sizeof labels[0]; ++i) {
		goto *labels[i];

lab0:
		puts("0");
		goto endloop;
lab1:
		puts("1");
		goto endloop;
lab2:
		puts("2");
		goto endloop;
lab3:
		puts("3");
endloop:
		;
	}

	return 0;
}

