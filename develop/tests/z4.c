#include <limits.h>

int
main(void) {
	char	buf[5][128];
	int	i;

	for (i = 0; i < 5; ++i) {
		int	j;
		for (j = 0; j < 128; ++j) {
			buf[i][j] = i + j;
		}
	}	
	for (i = 0; i < 5; ++i) {
		int	j;
		for (j = 0; j < 128; ++j) {
			printf("%d\n", buf[i][j]);
		}
	}	
}	

