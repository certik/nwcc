#ifndef CFGFILE_H
#define CFGFILE_H

struct ga_option;
struct cfg_option;

void	merge_argv_with_cfgfile(int *argc, char ***argv, struct ga_option
		*options, int nopts);
struct cfg_option *read_config_files(void);
char	**make_new_argv(char **old_argv, int *old_argc, struct cfg_option *opt,
		struct ga_option *options, int nopts);	

#endif

