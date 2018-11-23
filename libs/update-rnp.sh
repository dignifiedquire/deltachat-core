#!/bin/bash

cp_sources()
{
	SRCDIR=$1
	CDIR=$2
	mkdir -p               rnp/$CDIR/
	cp $SRCDIR/$CDIR/*.cpp rnp/$CDIR/ 2>/dev/null
	cp $SRCDIR/$CDIR/*.h   rnp/$CDIR/ 2>/dev/null
	cp $SRCDIR/$CDIR/TODO* rnp/$CDIR/ 2>/dev/null

	# write config.h to each source directory;
	# this way it should be guaranteed
	# that the correct file is picked up.
	echo "// generated file"              >  rnp/$CDIR/config.h
	echo "#undef  HAVE_BZLIB_H"           >> rnp/$CDIR/config.h
	echo "#define HAVE_ZLIB_H          1" >> rnp/$CDIR/config.h
	echo "#define HAVE_FCNTL_H         1" >> rnp/$CDIR/config.h
	echo "#define HAVE_INTTYPES_H      1" >> rnp/$CDIR/config.h
	echo "#define HAVE_LIMITS_H        1" >> rnp/$CDIR/config.h
	echo "#define HAVE_STDINT_H        1" >> rnp/$CDIR/config.h
	echo "#define HAVE_STRING_H        1" >> rnp/$CDIR/config.h
	echo "#define HAVE_SYS_CDEFS_H     1" >> rnp/$CDIR/config.h
	echo "#define HAVE_SYS_MMAN_H      1" >> rnp/$CDIR/config.h
	echo "#define HAVE_SYS_RESOURCE_H  1" >> rnp/$CDIR/config.h
	echo "#define HAVE_SYS_STAT_H      1" >> rnp/$CDIR/config.h
	echo "#define HAVE_SYS_TYPES_H     1" >> rnp/$CDIR/config.h
	echo "#define HAVE_UNISTD_H        1" >> rnp/$CDIR/config.h
}

update_rnp() {
	SRCDIR=$1
	
	# copy misc.

	cp $SRCDIR/LICENSE* rnp/

	# copy source
	
	rm -r rnp/src/*
	
	cp_sources $SRCDIR "src/lib"
	cp_sources $SRCDIR "src/lib/crypto"
	cp_sources $SRCDIR "src/librekey"
	cp_sources $SRCDIR "src/librepgp"
	cp_sources $SRCDIR "src/rnp"

	# copy header files

	rm -r rnp/include/*
	
	cp_sources $SRCDIR "include"	
	cp_sources $SRCDIR "include/rekey"	
	cp_sources $SRCDIR "include/repgp"	
	cp_sources $SRCDIR "include/rnp"	
}

if [ -n "$1" ]; then 
	update_rnp $1
else
	echo "usage: update-rnp <source-path>"
fi

