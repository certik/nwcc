#!/bin/sh

# 12/13/07: Added some more Solaris nonsense because the bullshit
# default shell is too disabled to handle any scripts at all
# including this one. From now on I'll NEVER rely on Solaris sh
# being capable of doing anything except launching /usr/xpg4/bin/sh
# :-( :-( :-(
if test `uname -s` = SunOS; then
	if test "$RECURSIVE" = ""; then
		RECURSIVE=yes
		export RECURSIVE
		/usr/xpg4/bin/sh ./install.sh
		exit
	fi
fi

INSTALLDIR=`grep INSTALLDIR config.h | awk -F\" '{ print $2 }'`
if test "$INSTALLDIR" = ""; then
	INSTALLDIR="/usr/local"
fi

INSTALLDIR=$DESTDIR$INSTALLDIR

if ! test -d $INSTALLDIR/bin; then
	echo "Warning: $INSTALLDIR/bin does not exist"
	if ! test -d "$INSTALLDIR"; then
		echo "Creating $INSTALLDIR"
		mkdir "$INSTALLDIR" 
	fi
	if ! test -d "$INSTALLDIR/lib"; then
		echo "Creating $INSTALLDIR/lib"
		mkdir "$INSTALLDIR/lib"
	fi
	if ! test -d "$INSTALLDIR/bin"; then
		echo "Creating $INSTALLDIR/bin"
		mkdir "$INSTALLDIR/bin"
	fi
fi

if ! test -d "$INSTALLDIR/nwcc"; then
	if ! mkdir "$INSTALLDIR/nwcc"; then
		exit 1
	fi
	if ! mkdir "$INSTALLDIR/nwcc/bin"; then
		exit 1
	fi
	if ! mkdir "$INSTALLDIR/nwcc/lib"; then
		exit 1
	fi	
fi

if test -f "$INSTALLDIR/bin/nwcc"; then
	mv "$INSTALLDIR/bin/nwcc" "$INSTALLDIR/bin/nwcc.old"
	mv "$INSTALLDIR/bin/nwcc1" "$INSTALLDIR/bin/nwcc1.old"
	rm "$INSTALLDIR/lib/libnwcc.o"
	rm -f "$INSTALLDIR/lib/libnwcc64.o"
	rm -f "$INSTALLDIR/lib/dynlibnwcc64.o"
	rm -f "$INSTALLDIR/lib/dynlibnwcc.o"
fi	
if ! cp nwcc nwcc1 "$INSTALLDIR/nwcc/bin" \
	|| ! ln -s "../nwcc/bin/nwcc" "$INSTALLDIR/bin/nwcc" \
	|| ! ln -s "../nwcc/bin/nwcc1" "$INSTALLDIR/bin/nwcc1"; then
	echo "Cannot install nwcc"
	exit 1
fi
if test -f cpp/nwcpp; then
	if test -d "$INSTALLDIR/nwcc/include"; then
		mv "$INSTALLDIR/bin/nwcpp" "$INSTALLDIR/bin/nwcpp.old"
		if test -d "$INSTALLDIR/nwcc/include.old"; then
			rm -rf "$INSTALLDIR/nwcc/include.old"
		fi
		mv "$INSTALLDIR/nwcc/include" "$INSTALLDIR/nwcc/include.old"
	fi

	if ! cp cpp/nwcpp "$INSTALLDIR/nwcc/bin" \
		|| ! ln -s "../nwcc/bin/nwcpp" "$INSTALLDIR/bin/nwcpp" \
		|| ! cp -R cpp/include "$INSTALLDIR/nwcc/include"; then
		echo "Warning: cannot install nwcpp"
	fi
fi	

cp snake "$INSTALLDIR/nwcc/bin"


# 07/30/09: Install library function catalog file (maybe we should use
# a different dir like "misc" or "share"?! lib is ok for now.
if test -f fcatalog.idx; then
	cp fcatalog.idx "$INSTALLDIR/nwcc/lib/fcatalog.idx"
fi


cp extlibnwcc.o "$INSTALLDIR/nwcc/lib/libnwcc.o"
cp dynextlibnwcc.o "$INSTALLDIR/nwcc/lib/dynlibnwcc.o"
if test -f extlibnwcc64.o; then
	cp extlibnwcc64.o "$INSTALLDIR/nwcc/lib/libnwcc64.o"
	cp dynextlibnwcc64.o "$INSTALLDIR/nwcc/lib/dynlibnwcc64.o"
fi

# 12/13/07: This was missing :-( crt1 for Solaris ...
if test -f crt1-32.o; then
	cp crt1-32.o "$INSTALLDIR/nwcc/lib/crt1-32.o"
fi	
if test -f crt1-64.o; then
	cp crt1-64.o "$INSTALLDIR/nwcc/lib/crt1-64.o"
fi

# 01/18/09: crtbegin and crtend for Linux/PPC (taken from gcc for now)
if test -f crtbegin-64.o; then
	cp crtbegin-64.o "$INSTALLDIR/nwcc/lib/crtbegin-64.o"
fi
if test -f crtbegin-32.o; then
	cp crtbegin-32.o "$INSTALLDIR/nwcc/lib/crtbegin-32.o"
fi
if test -f crtend-64.o; then
	cp crtend-64.o "$INSTALLDIR/nwcc/lib/crtend-64.o"
fi
if test -f crtend-32.o; then
	cp crtend-32.o "$INSTALLDIR/nwcc/lib/crtend-32.o"
fi


sys=`uname -s`
if test "$sys" = "IRIX" || test "$sys" = "IRIX64" ; then
	ln -s "../nwcc/lib/libnwcc.o" /usr/lib32/
else
 	ln -s "../nwcc/lib/libnwcc.o" "$INSTALLDIR/lib/"
	ln -s "../nwcc/lib/dynlibnwcc.o" "$INSTALLDIR/lib/"
	if test -f extlibnwcc64.o; then
		ln -s "../nwcc/lib/libnwcc64.o" "$INSTALLDIR/lib/"
		ln -s "../nwcc/lib/dynlibnwcc64.o" "$INSTALLDIR/lib/"
	fi
fi
echo
echo "nwcc has been installed to $INSTALLDIR/nwcc/ and $INSTALLDIR/bin/"
echo "(Do not forget to type \`rehash' if you're using csh.)"


