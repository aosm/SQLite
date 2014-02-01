##
# Makefile for SQLite3
##

# Project info
Project  = SQLite3
UserType = Developer
ToolType = Commands

GnuAfterInstall = fixup


# It's a GNU Source project
include $(MAKEFILEPATH)/CoreOS/ReleaseControl/GNUSource.make

Install_Target = install

# building in ~rc has to avoid ~ as a separator since it's used in pathnames
export LIBTOOL_CMD_SEP = +

# For building fat
export MACOSX_DEPLOYMENT_TARGET = 10.4
export LD_TWOLEVEL_NAMESPACE = 1
LDFLAGS += -sectorder __TEXT __text $(SRCROOT)/libsqlite3.order

# SQLite3 configuration options
Extra_Configure_Flags += --enable-threadsafe --enable-tcl

# add -DSQLITE_DEBUG=1 to enable lock tracing and other debugging features
# you also need to set sqlite3_os_trace to 1 in SQLite3/src/os_common.h
Extra_CC_Flags += -DASSERT_VIA_CALLBACK=1 -DENABLE_LOCKING_CALLBACKS=1

## this is ugly, we really need to patch configure/make/ltmain.sh
# can't strip libsqlite3.a correctly so we kill it
fixup:
	@echo "Trashing RC undesirable files"
	${RM} -rf $(RC_Install_Prefix)/lib/*.*a $(RC_Install_Prefix)/lib/pkgconfig; \
	${STRIP} -SXx $(RC_Install_Prefix)/bin/sqlite3; \
	${STRIP} -SXx $(RC_Install_Prefix)/lib/libsqlite3.0.dylib; \
	${STRIP} -SXx $(RC_Install_Prefix)/lib/sqlite3/libtclsqlite3.dylib; \
	${MKDIR} $(RC_Install_Prefix)/local/include; \
	${CP} $(Sources)/src/NSSQLiteLockingSupport.h $(RC_Install_Prefix)/local/include/;\
	${CHOWN} root:wheel $(RC_Install_Prefix)/local/include/NSSQLiteLockingSupport.h; \
	${CHMOD} 644 $(RC_Install_Prefix)/local/include/NSSQLiteLockingSupport.h; \
	${MKDIR} $(RC_Install_Man)/man1; \
	${CP} $(Sources)/sqlite3.1 $(RC_Install_Man)/man1/sqlite3.1; \
	${CHOWN} root:wheel $(RC_Install_Man)/man1/sqlite3.1; \
	${CHMOD} 644 $(RC_Install_Man)/man1/sqlite3.1; \
	${MKDIR} $(RC_Install_Prefix)/local/OpenSourceVersions/; \
	$(CP) $(Sources)/../SQLite.plist $(RC_Install_Prefix)/local/OpenSourceVersions/; \
	${CHOWN} root:wheel $(RC_Install_Prefix)/local/OpenSourceVersions/SQLite.plist; \
	${CHMOD} 644 $(RC_Install_Prefix)/local/OpenSourceVersions/SQLite.plist; \
	${MKDIR} $(RC_Install_Prefix)/local/OpenSourceLicenses/; \
	$(CP) $(Sources)/../SQLite.txt $(RC_Install_Prefix)/local/OpenSourceLicenses/; \
	${CHOWN} root:wheel $(RC_Install_Prefix)/local/OpenSourceLicenses/SQLite.txt; \
	${CHMOD} 644 $(RC_Install_Prefix)/local/OpenSourceLicenses/SQLite.txt;
