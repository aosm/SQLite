#!/bin/sh
BUILDDIR=/tmp/Build
xcodebuild -target th3_fulltest -sdk / -configuration Release OBJROOT=${BUILDDIR}/th3_obj SYMROOT=${BUILDDIR}/th3_sym build
