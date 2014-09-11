#include <limits.h>

int
main(void) {
	char	buf[5][128];
	int	i;
	int	j;

	for (i = 0; i < 5; ++i) {
		for (j = 0; j < 128; ++j) {
			buf[i][j] = i + j;
		}
	}	
#if 0
	buf[1][1] = 13;
	printf("%d\n", buf[1][1]);
#endif
	for (i = 0; i < 5; ++i) {
		for (j = 0; j < 128; ++j) {
			printf("%d\n", buf[i][j]);
		}
	}	
}	

