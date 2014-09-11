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

	s.num_entries = 0;
	++s.num_entries;
	printf("%d\n", s.num_entries);
	s.num_entries++;
	printf("%d\n", s.num_entries);
	printf("%d\n", ++s.num_entries);
	printf("%d\n", s.num_entries++);
	printf("%d\n", s.num_entries);
	s.num_entries = 128;
	printf("%d\n", s.num_entries++);
	printf("%d\n", s.num_entries);
	s.num_entries = 128;
	printf("%d\n", s.num_entries--);
	printf("%d\n", --s.num_entries);
}


