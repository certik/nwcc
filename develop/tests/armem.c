
#include <stdio.h>

struct bleh {
	int	type;
	char	*name;
};

struct hm {
	int		x;
	struct bleh	*ar[5];
	int		y;
};

void
foo(struct bleh *b)
{
	printf("%d, %s\n", b->type, b->name);
}

int
main(void)
{
	struct bleh	b = { 123, "hello" };
	struct hm	h;
	struct hm	*hp = &h;
	int		i;
	h.ar[0] = &b;
	i = 0;
	foo(hp->ar[i]);
	return 0;
}	



