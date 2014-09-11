#include <stdio.h>

int
main() {
	int i;
	static const char * const format_names[256] = {
		[11] = "aligned absolute",
		[1] = "uleb128",
		[9] = "sleb128",
		"pcrel sdata4",
		"pcrel sdata5",
		[8] = "lolz",
		"gnuuuuu"
	};

	printf("size = %d\n", sizeof format_names);

	for (i = 0; i < 0x20; ++i) {
		printf("%d", i);
		if (format_names[i] != NULL) {
			printf("      =  %s", format_names[i]);
		}
		putchar('\n');
	}
}



