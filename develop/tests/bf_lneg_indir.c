struct st_table {
    unsigned int entries_packed : 1;
    unsigned long num_entries : (sizeof(int) * 8) - 1;
};

#define MAX_PACKED_NUMHASH 128

int
main() {
	struct st_table	tab;
	struct st_table *table = &tab;
	int i;

	tab.entries_packed = 0;
	tab.num_entries = 17;

	printf("%d\n", !table->num_entries);
	printf("%d\n", !!table->num_entries);
	printf("%d\n", !!!table->num_entries);

	tab.entries_packed = 1;
	printf("%d\n", !table->num_entries);
	printf("%d\n", !!table->num_entries);
	printf("%d\n", !!!table->num_entries);
}

