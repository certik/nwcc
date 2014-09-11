#include <stdio.h>

int
main() {
	struct foo {
		char	buf[1000000];
		int	x;
	};
	int	i;

	struct foo		auto_foo;
	static struct foo	static_foo;
	struct foo		*ptr_foo = &auto_foo;

	auto_foo.x = 123;
	i = auto_foo.x;
	printf("%d\n", i);
	static_foo.x = 456;
	i = static_foo.x;
	printf("%d\n", i);
	ptr_foo->x = 789;
	i = ptr_foo->x;
	printf("%d\n", i);
	return 0;
}
	
