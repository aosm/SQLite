# buildroot.sh
#
# Performs B&I style quad-fat build of sqlite and creates a well-named root
# echos recommended commands to copy the root to the standard xdesign root folder
#
# Usage: . buildroot.sh [ -tcl | -embedded ]
# 
# buildroot.sh should only be invoked from the root src directory
#

RELEASE="SnowLeopard"
ARCHS="-arch ppc -arch i386 -arch x86_64"
SDKROOT=""
TARGET="SQLite"
ROOTPKGSTYLE="NORMAL"

if [ "$@ " == "-tcl " ]; then
  TARGET="SQLite_tcl"
fi

if [ "$@ " == "-embedded " ]; then
  RELEASE="Kirkwood"
  ARCHS="-arch armv6 -arch armv7"
  SDKROOT="/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS3.0.Internal.sdk"
  TARGET="SQLite"
  ROOTPKGSTYLE="BINARIES"
fi

SRCROOT=`pwd`
PROJECT="SQLite"
BUILDENV=""

if [ "${SDKROOT} " != " " ]; then
  BUILDENV="SDKROOT=${SDKROOT}"
fi

if [ "${BUILDENV} " != " " ]; then
  BUILDIT_ARGS="-target ${TARGET} -release ${RELEASE} -project ${PROJECT} ${ARCHS} ${SRCROOT} -- ${BUILDENV}"
else
  BUILDIT_ARGS="-target ${TARGET} -release ${RELEASE} -project ${PROJECT} ${ARCHS} ${SRCROOT}"
fi

echo "Building: buildit ${BUILDIT_ARGS}"
sudo ~rc/bin/buildit ${BUILDIT_ARGS}

DATESTAMP=`date "+%Y%m%d"`
SQLITE_VERSION=`cat public_source/VERSION`

VERSIONPLIST="${SDKROOT}/System/Library/CoreServices/SystemVersion.plist"
VERSIONKEY="ProductBuildVersion"
BUILDVERSION=`grep -A 1 ${VERSIONKEY} ${VERSIONPLIST} | grep -v ${VERSIONKEY} | sed 's|.*<string>\(.*\)</string>.*|\1|g'`

ROOTBALL="${TARGET}_${SQLITE_VERSION}_${DATESTAMP}_${BUILDVERSION}.tgz"
SROOTBALL="${TARGET}-XX-${DATESTAMP}-${BUILDVERSION}.tgz"

ROOTDST="/tmp/${PROJECT}.roots/${PROJECT}~dst"

echo "Creating tarball from root in destination dir: ${ROOTDST}"

if [ ${ROOTPKGSTYLE} == "BINARIES" ]; then
  echo "( cd ${ROOTDST}; tar -cvzp -f /tmp/${ROOTBALL} --exclude='*local*' --exclude='*share*' --exclude='*include*' . )"
  ( cd ${ROOTDST}; tar -cvz -f /tmp/${ROOTBALL} --exclude='*local*' --exclude='*share*' --exclude='*include*' . )
else
  echo "( cd ${ROOTDST}; tar -cvzp -f /tmp/${ROOTBALL} . )"
  ( cd ${ROOTDST}; tar -cvz -f /tmp/${ROOTBALL} . )
fi


echo "---------------------"
echo "To copy a NON-SUBMITTED root:"
echo "  sudo -u xdesign cp /tmp/${ROOTBALL} ~xdesign/roots/sqlite/${ROOTBALL}"
echo ""
echo "To copy a SUBMITTED root (replace 'XX' with the submission number)"
echo "  sudo -u xdesign cp /tmp/${ROOTBALL} ~xdesign/roots/sqlite/submission/${SROOTBALL}"
echo ""
