extern void (*kargp_program_version_hook) ();

void
bs() {
	puts("this is bs");
}	

int main()
{
	kargp_program_version_hook = bs;
	(*kargp_program_version_hook) ();
}

void (*kargp_program_version_hook) ();
