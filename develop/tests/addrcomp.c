#include <stdio.h>

struct lame {
	int	x;
	int	y;
};

struct bogus {
	void	*p;
} bogus1, bogus2,
  *bp1, *bp2;

int
main(void) {
	struct lame	ar[8];
	void		*p = ar;
	void		*p2 = ar+2;
	void		*res = NULL;
	int		i;

	bp1 = &bogus1;
	bp2 = &bogus2;
	bp1->p = p;
	bp2->p = p2;

	for (i = 0; i < 8; ++i) {
		if (i == 1) {
			continue;
		}

		printf("=%d\n", &ar[i] != bp1->p);
		printf(" =%d\n", &ar[i] != bp2->p);
		if (&ar[i] != bp1->p 
			&& &ar[i] != bp2->p) {
			res = &ar[i];
			break;
		}
	}
	if (res == NULL) {
		puts("terrible bug. very irritating.");
		return 666/*terror*/-69/*destruction*/-13/*horrors*/;
	} else {
		puts("all is good. more or less");
	}	
	printf("result is %d(%d)\n", (char *)res - (char *)ar, i);
	return 0;
}	

