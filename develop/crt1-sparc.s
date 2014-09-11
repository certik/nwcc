! Written by Nils R. Weller on Dec 20 2006
! Hacked and kludged on various subsequent days
!
! crt1.o implementation for 32bit and 64bit Solaris/SPARC. Tested on
! Solaris 10.
! This has been kludged up by means of ``trial and error''; There's
! probably some stuff missing. Assembler must be invoked with -P and
! -DSPARC_PTR_SIZE=... and -DSPARC_PTR_SCALING=....

.section ".text"
.proc 022
.global _start

#if SPARC_PTR_SIZE == 4
#  define STOREREG st
#  define ARGC_BOTCH 0
#  define LOADREG ld
#  define BIAS 0
#else
#  define STOREREG stx
#  define ARGC_BOTCH 4  /* four more bytes needed, guess it's right-adjusted */
#  define LOADREG ldx
#  define BIAS 2047 
#endif

#define ARGC_OFFSET (16 * SPARC_PTR_SIZE + ARGC_BOTCH + BIAS)
#define ARGV_OFFSET (17 * SPARC_PTR_SIZE + BIAS)

_start:
	! load argc
	ld [%sp + ARGC_OFFSET], %o0

	! load argv
	add %sp, ARGV_OFFSET, %o1 

	! get environ (argv+argc)
	! (scale argc for sizeof(char *) first)
	mov SPARC_PTR_SCALING, %l0
	mov %o0, %l1
	add %l1, 1, %l1 
	sll %l1, %l0, %l1
	add %o1, %l1, %o2

	! provide save area for regs, etc, so the caller stack
	! containing our valuable argc/argv/environ data doesn't
	! get trashed. I'm not sure how much is really needed yet 
	sub %sp, 128, %sp

	! Save argc, argv, environ
	STOREREG %o0, [%sp + BIAS+64]                                ! argc
	STOREREG %o1, [%sp + BIAS+64+SPARC_PTR_SIZE]                 ! argv 
	STOREREG %o2, [%sp + BIAS+64+SPARC_PTR_SIZE+SPARC_PTR_SIZE]  ! environ
	
	! perform initialization
	call _init, 0
	nop

	! atexit(_fini);
#if SPARC_PTR_SIZE == 4
	sethi %hi(_fini), %g1
	or %g1, %lo(_fini), %o0
#else
	sethi %hh(_fini), %l0
	sethi %lm(_fini), %l1 
	or %l0, %hm(_fini), %l0
	sllx %l0, 32, %l0
	add %l0, %l1, %l0
	or %l0, %lo(_fini), %o0
#endif

	call atexit, 0
	nop

	! Now reload argc, argc, environ and call main()
	LOADREG [%sp + BIAS+64], %o0                                ! argc
	LOADREG [%sp + BIAS+64+SPARC_PTR_SIZE], %o1                 ! argv
	LOADREG [%sp + BIAS+64+SPARC_PTR_SIZE+SPARC_PTR_SIZE], %o2  ! environ

	call main, 0 
	nop

	! 12/13/07: Woah this was missing!!! Didn't preserve the exit status
	mov %o0, %i0

	call exit, 0
	nop

