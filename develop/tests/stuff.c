#include <stdio.h>
#include <stdlib.h>

struct dec {
	size_t	nslots;
	int	*data;
};

void
foo(struct dec *destdec)  {
	int	i;

	destdec->nslots = 1;
	destdec->data = NULL;

	for (i = 0; i < 5; ++i) {
		destdec->nslots *= 2;
		destdec->data = realloc(destdec->data,
			/*sizeof *destdec->data * destdec->nslots*/20);
		destdec->data[i] = i;
	}

	for (i = 0; i < 5; ++i) {
		printf("%d\n", destdec->data[i]);
	}
}

int
main(void) {
	struct dec	d;
	foo(&d);
	return 0;
}	
			
	
