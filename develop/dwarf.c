#include "dwarf.h"
#include <stdio.h>
#include <string.h>
#include "n_libc.h"

#if 0
static struct in_file {
	const char	*name;
	int		id;
	struct in_file	*next;
} *files_head, *files_tail;
#endif
struct dwarf_in_file		*dwarf_files;
static struct dwarf_in_file	*dwarf_files_tail;

int
dwarf_put_file(const char *name) {
	struct dwarf_in_file	*inf;
	static int		curid = 1;

	if (name[0] == '<') {
		/* Bogosity like <built-in> or <command line> */
		return 0;
	}
	
	for (inf = dwarf_files; inf != NULL; inf = inf->next) {
		if (strcmp(inf->name, name) == 0) {
			return inf->id;
		}
	}

	/* Not in list yet */
	inf = n_xmalloc(sizeof *inf);
	inf->id = curid++;
	inf->name = name;
	inf->next = NULL;
	if (dwarf_files == NULL) {
		dwarf_files = dwarf_files_tail = inf; 
	} else {
		dwarf_files_tail->next = inf;
		dwarf_files_tail = inf;
	}
	return inf->id;
}

