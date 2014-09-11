enum backup_type
{
  no_backups,
  numbered_existing_backups,
};

static const enum backup_type backup_types[] =
{
  numbered_existing_backups 
};

enum backup_type
get_bs ()
{
  return numbered_existing_backups;
}

int
main() {
	printf("%d\n", (int)get_bs());
}

