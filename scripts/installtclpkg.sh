#!/bin/sh

# installtclpkg.sh
# SQLite
#
# Created by Adam on 1/20/09.
# Copyright 2009 Apple, Inc. All rights reserved.

# Generate the pkgIndex.tcl file for the libtclsqlite.dylib in the 

PACKAGEINDEX_PATH="$INSTALL_DIR/pkgIndex.tcl"

SQLITE_VERSION=`cat $SQLITE_VERSION_FILE`
TCL_PACKAGE_NAME="sqlite3"
LOAD_PACKAGE_NAME="Tclsqlite3"
LOAD_SCRIPT="[list load [file join \$dir $EXECUTABLE_NAME] $LOAD_PACKAGE_NAME]"
PACKAGE_SCRIPT="package ifneeded $TCL_PACKAGE_NAME $SQLITE_VERSION $LOAD_SCRIPT"

mkdir -p $INSTALL_DIR
echo "$PACKAGE_SCRIPT" > "$PACKAGEINDEX_PATH"

echo -n "Generated tcl index package file in $PACKAGEINDEX_PATH: " 
cat "$PACKAGEINDEX_PATH"