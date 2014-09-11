typedef enum
{
	STUFF,
	STUFF2

} bst_t;

int
main() {
	struct fu {
		bst_t type:8;
	} f;

	f.type = STUFF2;
	printf("%d\n", f.type);
}



