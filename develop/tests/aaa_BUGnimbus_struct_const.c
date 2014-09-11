#include <stdio.h>

typedef struct {
	char	*key;
	char	*value;
} T1;

typedef struct {
	long	type;
	char	*value;
} T2;

T1	a[] = {
	{
		"",
		((char *) &((T2){1, 0}))
	}
};	

int
main() {
	T2	*p = (T2 *)a[0].value;
	printf("%ld %p\n", p->type, p->value);
	return 0;
}

