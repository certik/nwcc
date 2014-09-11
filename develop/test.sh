#!/bin/sh

if test `uname -s` = "SunOS"; then
	if test "$SHELL" != "ksh"; then
		NWCC_ASM=as
		SHELL=ksh
		export NWCC_ASM
		ksh ./test.sh "$@"
		exit "$?"
	fi
fi


# 07/30/09: NOTE!!!!!!!! The function below doesn't tell the truth, nasm
# hasn't been the default assembler for years. gas is the default and
# the function isn't called anymore
test_nasm() {
if ! nasm -v >/dev/null 2>/dev/null; then
	echo "The NASM Netwide Assembler does not seem to be"
	echo "installed on this system. It is the default"
	echo "assembler used by nwcc on x86, so you should"
	echo "install it. You can download it from"
	echo
	echo "http://sourceforge.net/projects/nasm/"
	echo
	echo "... then install it, and rerun ./test.sh"
	echo
	echo ".... HOWEVER, if you'd like you try out nwcc"
	echo "without NASM, you can also make it generate"
	echo "gas code by setting the environment variable"
	echo "NWCC_ASM to gas;"
	echo
	echo "export NWCC_ASM=gas       - for bash/ksh/sh"
	echo "setenv NWCC_ASM gas       - for tcsh/csh"
	exit 1
fi	
}	

# 07/30/09: NOTE!!!!!!!! The function below doesn't tell the truth, yasm
# hasn't been the default assembler for years. gas is the default and
# the function isn't called anymore
test_yasm() {
	if ! yasm --version >/dev/null 2>/dev/null; then
		echo "The YASM Modular Assembler does not seem to be"
		echo "installed on this system. It is needed because"
		echo "nwcc on x86-64 generates code for YASM, not gas."
		echo "You can download it from"
		echo
		echo "http://www.tortall.net/~yasm/"
		echo "... then install it, and rerun ./test.sh"
		echo
		echo ".... HOWEVER, if you'd like you try out nwcc"
		echo "without YASM, you can also make it generate"
		echo "gas code by setting the environment variable"
		echo "NWCC_ASM to gas;"
		echo
		echo "export NWCC_ASM=gas       - for bash/ksh/sh"
		echo "setenv NWCC_ASM gas       - for tcsh/csh"
		exit 1
	fi
}

SYS=`uname -s`
MACH=`uname -m`

if test "$SYS" = "Linux" || test "$SYS" = "FreeBSD" || \
	test "$SYS" = "OpenBSD"; then
	if test "$MACH" = "x86_64" || test "$MACH" = "amd64"; then
		if test "$NWCC_ASM" != ""; then
			BASEASM=`basename $NWCC_ASM`
		fi	
		if test "$BASEASM" != gas && test "$BASEASM" != as; then
			: test_yasm
		fi	
	else
		if test "$NWCC_ASM" != ""; then
			BASEASM=`basename $NWCC_ASM`
		fi	
		if test "$BASEASM" != gas && test "$BASEASM" != as; then
			: test_nasm
		fi	
	fi
fi

if ! cd tests; then exit 1; fi
cp ../nwcc .
cp ../nwcc1 .
cp ../snake .

# ./extlibnwcc.o will be used if /usr/local/nwcc/lib/libnwcc.o does
# not exist
cp ../extlibnwcc.o .
if test -f ../extlibnwcc64.o; then
	cp ../extlibnwcc64.o .
fi
if test -f ../crt1-32.o; then
	cp ../crt1-32.o .
fi
if test -f ../crt1-64.o; then
	cp ../crt1-64.o .
fi

SUPRESS="kludge"

for i in "$@"; do
	if test "$i" = "--verbose" || test "$i" = "-v"; then
		SUPRESS=""
	elif test "$i" = "-maix64" || test "$i" = "--64" \
		|| test "$i" = "-m64" \
		|| test "$i" = "-xarch=v9" || test "$i" = "-mabi=64"; then
		if test `uname -s` = AIX; then
			NWCC_CFLAGS="-mabi=aix64"
			GCC_CFLAGS="-maix64"
		elif test `uname -s` = IRIX64; then
			NWCC_CFLAGS="-mabi=n64"
			GCC_CFLAGS="-mabi=64"
		elif test `uname -s` = SunOS; then
			GCC_CFLAGS="-m64"
		elif test `uname -s` = Darwin; then
			GCC_CFLAGS="-m64"
		else
			echo "--64 is not supported on this platform"
			exit 1
		fi	
	else
		echo "Unknown option $i"
		exit 1
	fi
done	


if test `uname -p` = sparc; then
	GCC_CFLAGS="-m64"
fi

try_files() {
	if ! gcc $GCC_CFLAGS $FILES >/dev/null 2>/dev/null; then
		echo "gcc error"
		continue
	fi	

	echo $INPUT | ./a.out >output
	rm a.out

	if test "$SUPRESS" = ""; then
		./nwcc $NWCC_CFLAGS $i 
	else
		./nwcc $NWCC_CFLAGS $i >/dev/null 2>/dev/null
	fi

	if ! test -f a.out; then
		echo "NWCC ERROR"
		continue
	fi	

	echo $INPUT | ./a.out >output2
	DIFF=`diff output output2`
	if test "$DIFF" != ""; then
		echo BAD CODE
	else
		echo OK
	fi
}

echo This script will test a few compiler features. The files in
echo the "tests" sub directory are compiled with nwcc and gcc,
echo and the output of the resulting programs is compared.
echo
echo There are `ls *.c | wc -l` test files!
echo
echo Note that on non-x86-platforms, at least a few files should
echo fail, such as asmtest.c and structvaarg.c 
echo

for i in `ls *.c`; do
	printf "Trying $i ... "

	FILES="$i"
	INPUT="some stuff for input"
	try_files
done

rm a.out output output2 *.asm ; cd ..

echo
echo "All done, you may type \`make install' now"
