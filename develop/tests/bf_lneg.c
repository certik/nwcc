#include <stdio.h>


struct st_table {
    unsigned int entries_packed : 1;
    unsigned int num_entries : 31; 
};


/*
 * 08/04/09: The ++ operator didn't work for bitfields
 */
int
main() {
	struct st_table	s;

	s.entries_packed = 0;
	s.num_entries = 127;

	printf("%d\n", !s.num_entries);
	printf("%d\n", !!s.num_entries);
	printf("%d\n", !!!s.num_entries);

	/*
	 * Now check that other members in the storage unit do not
	 * affect the outcome
	 */
	s.entries_packed = 1;
	printf("%d\n", !s.num_entries);
	printf("%d\n", !!s.num_entries);
	printf("%d\n", !!!s.num_entries);

	printf("%d\n", !s.entries_packed);
	printf("%d\n", !!s.entries_packed);
	printf("%d\n", !!!s.entries_packed);

	s.num_entries = 0;

	printf("%d\n", !s.entries_packed);
	printf("%d\n", !!s.entries_packed);
	printf("%d\n", !!!s.entries_packed);
}


