#!/bin/sh

# installtcldyliblinks.sh
# SQLite
#
# Created by Adam on 1/20/09.
# Copyright 2009 Apple, Inc. All rights reserved.

# creates all of the required tcl dylib symlinks for backwards compatibility

DYLIB_BASE_DIR="/usr/lib"
LEGACY_TCL_DYLIB_DIR="$DYLIB_BASE_DIR/sqlite3"

mkdir -p $DSTROOT$DYLIB_BASE_DIR
rm -f "$DSTROOT$LEGACY_TCL_DYLIB_DIR"
ln -s "$INSTALL_PATH" "$DSTROOT$LEGACY_TCL_DYLIB_DIR"

echo -n "Added symlink from $INSTALL_PATH to $DSTROOT$LEGACY_TCL_DYLIB_DIR" 
