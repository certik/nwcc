#include <stdio.h>
#include <string.h>

int
main() {
	int	i = 19;
	char	buf[i];
	char	(*gnu)[i];
	
	gnu = &buf;

	strcpy(*gnu, "nonsense");
	puts(buf);

	if (gnu == &buf) {
		puts("gud");
	} else {
		puts("bad");
	}

	++gnu;
	if (gnu == &buf) {
		puts("bad2");
	} else {
		puts("gud2");
	}

	/* Verify that the increment worked properly */
	printf("%u\n", (unsigned)((char *)gnu - (char *)buf));

	--gnu;
	/* Verify that the decrement worked properly */
	printf("%u\n", (unsigned)((char *)gnu - (char *)buf));

	/* Try increment/decrement with +/- operators instead */
	gnu += 1;
	printf("%u\n", (unsigned)((char *)gnu - (char *)buf));
	gnu -= 1;
	printf("%u\n", (unsigned)((char *)gnu - (char *)buf));
}

