#ifndef CC_MAIN_H
#define CC_MAIN_H

extern int	stackprotectflag;
extern int	gnuheadersflag; /* XXX */
extern int	pedanticflag;
extern int	verboseflag;
extern int	nostdinc_flag;
extern int	nostdlib_flag;
extern char	*asmflag;
extern char	*asmname;
extern int	assembler;

extern char	*gnuc_version;
extern char	*cppflag;
extern int	archflag;
extern int	abiflag;
extern int	abiflag_default;
extern int	sysflag;
extern int	sysflag_default;
extern int	Oflag;

extern int	gflag;
extern int	oflag;
extern int	Eflag;
extern int	sflag;
extern int	Oflag;

extern int	sharedflag;
extern int	stupidtraceflag;
extern int	picflag;


extern char	*out_file;

extern int	*argmap;

extern int	timeflag;

extern int	mintocflag;
extern int	fulltocflag;

extern int	funsignedchar_flag;
extern int	fsignedchar_flag;
extern int	fnocommon_flag;

extern char	*custom_cpp_args;
extern char	*custom_ld_args;
extern char	*custom_asm_args;
extern char	*xlinker_args;

extern char	*std_flag;

extern int	notgnu_flag;
extern int	color_flag;

extern int	dump_macros_flag;

extern int	write_fcat_flag;
extern int	save_bad_translation_unit_flag;

#endif

