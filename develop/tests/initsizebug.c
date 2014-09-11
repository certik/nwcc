enum filetype
{
  none,	
  arg_directory
};

static const char	filetype_letter[] = "xy";

struct s {
	unsigned int	foo:(sizeof filetype_letter - 1 == arg_directory + 1)?
		1: -1;
};	

int
main() {
	printf("%d\n", arg_directory);
	printf("%d\n", (int)sizeof filetype_letter);
}



