#ifndef STANDARDS_H
#define STANDARDS_H

#define ISTD_NONE	0
#define ISTD_C89	1
#define ISTD_C99	2
#define ISTD_GNU89	3
#define ISTD_GNU99	4

#define ISTD_DEFAULT	ISTD_GNU99

int	option_to_std(const char *name);
int	set_default_std(void);
char	*get_selected_std_name(void);
int	using_strict_iso_c(void);

extern int	stdflag;

#endif

