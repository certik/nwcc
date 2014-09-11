#ifndef FCATALOGUE_H
#define FCATALOGUE_H

int
fcat_write_index_file(const char *dest, const char *src);
int
fcat_open_index_file(const char *path);
struct decl *
fcat_lookup_builtin_decl(const char *name, char **header, int standard);
struct type *
fcat_get_dummy_typedef(const char *name);


struct vreg;
struct ty_func;
struct type;
struct token;

void
check_format_string(struct token *tok,
	struct type *fty,
	struct ty_func *fdecl,
	struct vreg **args,
	struct token **from_consts,
	int nargs);

extern int	doing_fcatalog;


#endif


