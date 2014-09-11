#include <stdio.h>

int
main() {
	static const char * const format_names[256] = {
		[10] = "hello",
		[8] = "world",
		[3] = "lolz"
	};

	printf("size = %d\n", (int)sizeof format_names);
	{
		int i;
		for (i = 0; i < 11; ++i) {
			printf("%d:  %d\n", i, format_names[i] != NULL);
			if (format_names[i] != NULL) {
				printf("        >%s\n", format_names[i]);
			}
		}
	}
	return 0;
}



