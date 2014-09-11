#!/bin/sh

#
# This is a more "advanced" test suite to test more complex compilation setups
# and compiler flags. In particular, this suite tests ABI compatibility beween
# nwcc and other system compilers (the first suite mostly tests internal
# consistency).
#
# It will later also test things like PIC code

rm -f *.so *.o

if test `uname -s` = SunOS; then
        if test "$RECURSIVE" = ""; then
                RECURSIVE=yes
                export RECURSIVE
                /usr/xpg4/bin/sh ./test.sh "$@"
                exit
	fi
fi

for i in "$@"; do
	if test "$i" = "--sys"; then
		FULLCOMP=yes
		if test "$DONTCOMP" = yes; then
			echo "Error: --sys cannot be used with --nwcc"
			exit 1
		fi
		DONTCOMP=yes
	elif test "$i" = "--nwcc"; then
		DONTCOMP=yes
		if test "$FULLCOMP" = yes; then
			echo "Error: --sys cannot be used with --nwcc"
			exit 1
		fi
	elif test "$i" = "--verbose"; then
		SUPRESS=no
	else
		echo "Invalid option $i"
		echo
		echo "Usage:"
		echo
		echo "./test.sh           - Compare nwcc results with system compiler results"
		echo
		echo "./test.sh --sys     - Display system compiler results"
		echo
		echo "./test.sh --nwcc    - Display nwcc results"
		echo
		echo "--verbose           - Show make/compiler output"
		exit 1
	fi
done

make clean >/dev/null 2>/dev/null


getnwcc() {
cp ../nwcc .
cp ../nwcc1 .
if test -f ../crt1-64.o ; then
	cp ../crt1-64.o .
fi
if test -f ../crt1-32.o ; then
	cp ../crt1-32.o .
fi
}

getnwcc


# XXX we should make a "getcc" and "getcflags" script to determine a consistent comparison
# cc to use for configure, test.sh, second test.sh and perhaps install.sh!!!!
echo 'int main(void) { const int foo = 0; }' >_test.c
if test `uname -s` = "SunOS"; then
	if cc -o _test _test.c; then
		if test -x _test; then
			CCcomp=cc
			PICFLAGScomp=-KPIC
			SHAREDFLAGScomp=-G
		else
			CCcomp=gcc
		fi
	else
		CCcomp=gcc
	fi
	# CCcomp=cc

	if test `uname -p` = sparc; then
		if test "$CCcomp" = cc; then
			CCcompflags=-xarch=v9
		else
			CCcompflags=-m64
		fi
	fi
else
	if gcc -o _test _test.c; then
		CCcomp=gcc
	fi
fi
rm -f _test.c _test


if test "$CCcomp" = gcc; then
	PICFLAGScomp=-fPIC
	SHAREDFLAGScomp=-shared
fi


if test "$CCcomp" = ""; then
	echo Error: Cannot find usable system compiler!
	exit 1
fi

if test "$FULLCOMP" = yes; then
	# Use compare compiler for ALL files, no nwcc at all
	CC=$CCcomp
	CCflags=$CCcompflags
	PICFLAGS=$PICFLAGScomp
	SHAREDFLAGS=$SHAREDFLAGScomp
else
	CC=./nwcc
	CCflags=""
	PICFLAGS="-fpic"
	SHAREDFLAGS="-shared"
fi

origCCcomp=$CCcomp
origCCcompflags=$CCcompflags
origCC=$CC
origCCflags=$CCflags
origPICFLAGS=$PICFLAGS
origSHAREDFLAGS=$SHAREDFLAGS
origPICFLAGScomp=$PICFLAGScomp
origSHAREDFLAGScomp=$SHAREDFLAGScomp


resetcompilers() {
	# Reset comparison compilers and respective flags
	CCcomp=$origCCcomp
	CCcompflags=$origCCcompflags
	CC=$origCC
	CCflags=$origCCflags
	PICFLAGS=$origPICFLAGS
	SHAREDFLAGS=$origSHAREDFLAGS
	PICFLAGScomp=$origPICFLAGScomp
	SHAREDFLAGScomp=$origSHAREDFLAGScomp
	
	export CCcompflags
	export CCcomp
	export CC
	export CCflags
	export PICFLAGS
	export SHAREDFLAGS
	export PICFLAGScomp
	export SHAREDFLAGScomp
}

resetcompilers



dotest() {
	printf "$2 ... " 
	if test "$SUPRESS" = no; then
		echo
		make $1
	else
		make $1 >/dev/null 2>/dev/null
	fi

	if ! test -x $1; then
		echo NWCC ERROR
	else
		echo some stuff for input | LD_LIBRARY_PATH=. ./$1 >nwcc.out

		if test "$DONTCOMP" = yes; then
			# We are only asked to create and run the
			# program, presumably for nwcc debugging
			# purposes
			echo
			echo "---------- output: ----------"
			cat nwcc.out
			echo "-----------------------------"
			return
		fi


		# Now build whole test case with comparison
		# compiler to see whether the output differs
		# from nwcc

		CC=$CCcomp
		CCflags=$CCcompflags
		PICFLAGS=$PICFLAGScomp
		SHAREDFLAGS=$SHAREDFLAGScomp
		export CC
		export CCflags
		export PICFLAGS
		export SHAREDFLAGS

		make clean >/dev/null 2>/dev/null
		getnwcc

		if test "$SUPRESS" = no; then
			make $1
		else
			make $1 >/dev/null 2>/dev/null
		fi

		# Reset CC to nwcc
		resetcompilers

		if ! test -x $1; then
			echo CC ERROR
		else
			echo some stuff for input | LD_LIBRARY_PATH=. ./$1 >cc.out
			if test "`diff nwcc.out cc.out`" != ""; then
				echo BAD CODE
			else
				echo OK
			fi
		fi
		rm -f $1 nwcc.out cc.out
	fi
}

dotest test0         "ABI test 0 (float)                        "
dotest test1         "ABI test 1 (pass/ret big structs)         "
dotest test2         "ABI test 2 (pass/ret small structs)       "
dotest symbols0      "Global variables sanity test 0 (linking)  "
dotest tlstest0      "Thread-local storage test 0               "
dotest tlstest1      "Thread-local storage test 1               "
dotest pictest0      "Position-independent code test 0 (shobj)  "
dotest pictest1      "Position-independent code test 1 (init)   "
dotest pictest2      "Position-independent code test 2 (fptr)   "
dotest strpass0      "Struct passing test 1 (small)             "
dotest strpass1      "Struct passing test 2 (medium)            "
dotest strpass2      "Struct passing test 3 (small)  (other dir)"
dotest strpass3      "Struct passing test 4 (medium) (other dir)"
dotest std0          "Standards compatibility test 1 (C89)      "
dotest std1          "Standards compatibility test 2 (C89)      "
dotest common        "Common variables test                     "
dotest staticredec0  "Static function redeclaration test 0      "
dotest staticredec1  "Static function redeclaration test 1      "
