#!/bin/sh

# installdyliblinks.sh
# SQLite
#
# Created by Adam on 1/20/09.
# Copyright 2009 Apple, Inc. All rights reserved.

# creates all of the required dylib symlinks for backwards compatibility

#SQLITE_VERSION=`cat $SQLITE_VERSION_FILE`
LEGACY_DYLIB_PATH="$INSTALL_DIR/libsqlite3.0.dylib"

mkdir -p $INSTALL_DIR
rm -f "$LEGACY_DYLIB_PATH"
ln -s "$EXECUTABLE_NAME" "$LEGACY_DYLIB_PATH"

echo -n "Added dyld symlink to $LEGACY_DYLIB_PATH" 
