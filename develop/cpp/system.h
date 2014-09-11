#ifndef SYSTEM_H
#define SYSTEM_H

struct predef_macro;

void	set_type_macros(struct predef_macro **head, struct predef_macro **tail,
		int archflag, int abiflag, int sysflag);

void	set_system_macros(struct predef_macro **head, struct predef_macro **tail,
		int archflag, int abiflag, int sysflag);

#endif

