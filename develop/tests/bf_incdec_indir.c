struct st_table {
    const void *type;
    unsigned long num_bins;
    unsigned int entries_packed : 1;

    unsigned long num_entries : (sizeof(int) * 8) - 1;
#if 0
    struct st_table_entry **bins;
    struct st_table_entry *head, *tail;
#endif
};

#define MAX_PACKED_NUMHASH 128

int
main() {
	struct st_table	tab;
	struct st_table *table = &tab;
	int i;
	tab.num_entries = 17;

	i = table->num_entries++;
	printf("%d %d\n", i, table->num_entries);
}

