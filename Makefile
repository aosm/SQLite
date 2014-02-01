##
# Makefile for SQLite3
##

# Project info
Project  = SQLite3
UserType = Developer
ToolType = Commands

GnuAfterInstall = post_install_fixup


# It's a GNU Source project
include $(MAKEFILEPATH)/CoreOS/ReleaseControl/GNUSource.make

Install_Target = install

# building in ~rc has to avoid ~ as a separator since it's used in pathnames
export LIBTOOL_CMD_SEP = +

# For building fat
export MACOSX_DEPLOYMENT_TARGET = 10.5
export LD_TWOLEVEL_NAMESPACE = 1
# LDFLAGS += -sectorder __TEXT __text $(SRCROOT)/libsqlite3.order

# SQLite3 configuration options
Extra_Configure_Flags += --enable-threadsafe
# Extra_Configure_Flags += --enable-debug

# add -DSQLITE_DEBUG=1 to enable lock tracing and other debugging features
# you also need to set sqlite3_os_trace to 1 in SQLite3/src/os_common.h
Extra_CC_Flags += -g -O3 -DSQLITE_ENABLE_LOCKING_STYLE
Extra_CC_Flags += -DUSE_PREAD -fno-strict-aliasing
Extra_CC_Flags += -DSQLITE_MAX_LENGTH=2147483647 -DSQLITE_MAX_SQL_LENGTH=3000000 -DSQLITE_MAX_VARIABLE_NUMBER=500000

#Extra_CC_Flags += -faltivec -fasm-blocks -O3 -maltivec -falign-loops=16 -ftree-loop-linear -ftree-loop-im
#Extra_CC_Flags += -ftree-vectorize

export DSYMUTIL = dsymutil

post_install_fixup:
	# remove static libraries and pkgconfig
	${RM} -rf $(RC_Install_Prefix)/lib/*.*a $(RC_Install_Prefix)/lib/pkgconfig;
	# fix the permissions on the tcl dylib
	${CHMOD} 755 $(RC_Install_Prefix)/lib/sqlite3/libtclsqlite3.dylib; 
	# copy symbol-rich binary and libraries to SYMROOT
	${CP} $(RC_Install_Prefix)/bin/sqlite3 $(SYMROOT)/sqlite3 
	${CP} $(RC_Install_Prefix)/lib/libsqlite3.0.dylib $(SYMROOT)/libsqlite3.0.dylib 
	${CP} $(RC_Install_Prefix)/lib/sqlite3/libtclsqlite3.dylib $(SYMROOT)/libtclsqlite3.dylib 
	# create dSYM files
	${DSYMUTIL} $(SYMROOT)/sqlite3
	${DSYMUTIL} $(SYMROOT)/libsqlite3.0.dylib
	${DSYMUTIL} $(SYMROOT)/libtclsqlite3.dylib
	# strip installed binaries
	${STRIP} -S $(RC_Install_Prefix)/bin/sqlite3
	${STRIP} -S $(RC_Install_Prefix)/lib/libsqlite3.0.dylib
	${STRIP} -S $(RC_Install_Prefix)/lib/sqlite3/libtclsqlite3.dylib
	# setup man pages for installation
	${MKDIR} $(RC_Install_Man)/man1; 
	${CP} $(Sources)/sqlite3.1 $(RC_Install_Man)/man1/sqlite3.1; 
	${CHOWN} root:wheel $(RC_Install_Man)/man1/sqlite3.1; 
	${CHMOD} 644 $(RC_Install_Man)/man1/sqlite3.1; 
	# add legal/licensing docs
	${MKDIR} $(RC_Install_Prefix)/local/OpenSourceVersions/; 
	$(CP) $(Sources)/../SQLite.plist $(RC_Install_Prefix)/local/OpenSourceVersions/; 
	${CHOWN} root:wheel $(RC_Install_Prefix)/local/OpenSourceVersions/SQLite.plist; 
	${CHMOD} 644 $(RC_Install_Prefix)/local/OpenSourceVersions/SQLite.plist; 
	${MKDIR} $(RC_Install_Prefix)/local/OpenSourceLicenses/; 
	$(CP) $(Sources)/../SQLite.txt $(RC_Install_Prefix)/local/OpenSourceLicenses/; 
	${CHOWN} root:wheel $(RC_Install_Prefix)/local/OpenSourceLicenses/SQLite.txt; 
	${CHMOD} 644 $(RC_Install_Prefix)/local/OpenSourceLicenses/SQLite.txt; 
	# strip out the 64-bit architectures from the sqlite3 binary
	( ${LIPO} $(RC_Install_Prefix)/bin/sqlite3 -remove ppc64 -output $(RC_Install_Prefix)/bin/sqlite3; echo "Removed ppc64 arch from sqlite3 binary (if present)"; ) ; 
	( ${LIPO} $(RC_Install_Prefix)/bin/sqlite3 -remove x86_64 -output $(RC_Install_Prefix)/bin/sqlite3; echo "Removed x86_64 arch from sqlite3 binary (if present)"; ) ; 

