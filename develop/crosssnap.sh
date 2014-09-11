#!/bin/sh


# 05/20/11: Create a cross compilation dump for all supported architecture/
# system/ABI combinations from the current host.
# Input is expected in ./tests, output is written to ./crossdata, to a
# sub directory identifying this host, containing one sub directory per
# target.
#
# Note that this does not use target system headers yet (if and when it
# does, we need to generate code for all "minor" platforms too - see
# major flag in misc.c), and does not keep MIPS/MIPSel apart

if ! test -d crossdata; then
	mkdir crossdata
fi

MYID=`./nwcc --dump-my-sysid`
OUTDIR=crossdata/$MYID

if ! test -d $OUTDIR; then
	if ! mkdir $OUTDIR; then
		exit 1
	fi
fi

if ! test -d tests; then
	echo Cannot find tests directory!
	exit 1
fi

# Set field separator to newline (because we will get "--arch=x --sys=x --abi=x"
# whitespace separated tuples)
OLDIFS=$IFS
NLIFS="
"
IFS=$NLIFS

for i in `./nwcc --dump-all-arch-sys-abi-combinations`; do
	IFS=$OLDIFS

	echo Dumping $i ...
	CUROUTDIR=$OUTDIR/`./nwcc $i --dump-target-id`

	if ! test -d $CUROUTDIR; then
		if ! mkdir $CUROUTDIR; then
			exit 1
		fi
	fi

	for j in `ls tests/*.c`; do
		if ./nwcc -S $i $j >/dev/null 2>/dev/null; then
			mv `basename $j | sed 's/c$/asm/'` $CUROUTDIR/
		fi
	done
	IFS=$NLIFS
done

