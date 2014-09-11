typedef enum
{
  TYPE_NONE,
  TYPE_BOGUS,
} arg_type;

int
main()
{
	arg_type type;
	type = TYPE_NONE;
	type = (type);
	if (type == TYPE_NONE) {
		puts("bogus");
	} else {
		puts("Bug");
	}
	type = ((((((((TYPE_BOGUS))))))));
	if (type != TYPE_NONE) {
		puts("bogus");
	} else {
		puts("Bug");
	}
}

