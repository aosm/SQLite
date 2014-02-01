#!/bin/sh

# installprivatehdrs.sh
# SQLite
#
# Created by Local Adam on 1/15/10.
# Copyright 2010 __MyCompanyName__. All rights reserved.

# installs private headers

if [ "${CUSTOM_PRIVATE_HEADERS} " != " " -a \
     "${PRIVATE_HEADERS_FOLDER_PATH} " != " " ]; then
  echo "Copying private headers to '$PRIVATE_HEADERS_FOLDER_PATH'" 
  mkdir -pv ${DSTROOT}${PRIVATE_HEADERS_FOLDER_PATH}
  cp -v ${CUSTOM_PRIVATE_HEADERS} ${DSTROOT}${PRIVATE_HEADERS_FOLDER_PATH}
fi;

