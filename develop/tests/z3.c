#include <limits.h>

int
main(void) {
	char	buf[2][5][128];
	int	i;
	int	h;
	int	j;

	for (h = 0; h < 2; ++h)
		for (i = 0; i < 5; ++i) {
			for (j = 0; j < 128; ++j) {
				buf[h][i][j] = i + j;
			}
		}	
#if 0
	buf[1][1] = 13;
	printf("%d\n", buf[1][1]);
#endif

	for (h = 0; h < 2; ++h)
		for (i = 0; i < 5; ++i) {
			for (j = 0; j < 128; ++j) {
				printf("%d\n", buf[h][i][j]);
			}
		}	
}	

