#ifndef DWARF_H
#define DWARF_H

struct dwarf_in_file {
	const char		*name;
	int			id;
	struct dwarf_in_file	*next;
};

extern struct dwarf_in_file	*dwarf_files;

int	dwarf_put_file(const char *name);

#endif

