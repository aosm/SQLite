# buildroot.sh
#
# Performs B&I style quad-fat build of sqlite and creates a well-named root
# echos recommended commands to copy the root to the standard xdesign root folder
#
# Usage: . buildroot.sh [ -tcl | -embedded ]
# 
# buildroot.sh should only be invoked from the root src directory
#

RELEASE="Barolo"
OTHERARGS="-arch x86_64 -arch i386"
SDKROOT=""
TARGET="SQLite"
PROJECT="SQLite"
ROOTPKGSTYLE="NORMAL"
BUILD_CMD="buildit"
BUILD_TARGET="${TARGET}"

if [ "$@ " == "-tcl " ]; then
  TARGET="SQLite_tcl"
fi

if [ "$@ " == "-embedded " ]; then
  RELEASE="Telluride"
  BUILD_CMD="smartBuildit"
  OTHERARGS="-showLog"
  SDKVERSION="5.0"
  BUILD_TARGET=""
  ROOTPKGSTYLE="BINARIES"
fi

if [ "$@ " == "-simulator " ]; then
  RELEASE="Telluride"
  SDKVERSION="5.0"
# TARGET="libsqlite3_iPhoneSim"
  BUILD_TARGET=""
  TARGET="SQLite_Sim"
  PROJECT="SQLite_Sim"
  BUILD_CMD="smartBuildit"
  OTHERARGS="-showLog"
fi

echo -n "Release name [default: ${RELEASE}] ? " ; read
if [ "${REPLY} " != " " ]; then 
  RELEASE=${REPLY}
fi

if [ "$@ " == "-embedded " ]; then
  echo -n "SDK version [default: ${SDKVERSION}] ? " ; read
  if [ "${REPLY} " != " " ]; then 
    SDKVERSION=${REPLY}
  fi
  SDKROOT="/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS${SDKVERSION}.Internal.sdk"
fi

if [ "$@ " == "-simulator " ]; then
  echo -n "SDK version [default: ${SDKVERSION}] ? " ; read
  if [ "${REPLY} " != " " ]; then 
    SDKVERSION=${REPLY}
  fi
  SDKROOT="/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator${SDKVERSION}.sdk"
fi

SRCROOT=`pwd`
ROOTBASE="/tmp/${PROJECT}.roots"

BUILDIT_ARGS="-release ${RELEASE} -project ${PROJECT} ${OTHERARGS} ${SRCROOT}"
if [ "${BUILD_TARGET} " != " " ]; then
  BUILDIT_ARGS="-target ${BUILD_TARGET} ${BUILDIT_ARGS}"
fi

if [ -e "${ROOTBASE}" ]; then
  echo "Moving previous root to ${ROOTBASE}.prev"
  sudo rm -rf "${ROOTBASE}.prev"
  sudo mv "${ROOTBASE}" "${ROOTBASE}.prev"
fi

DATESTAMP=`date "+%Y%m%d"`
SQLITE_VERSION=`cat public_source/VERSION`

VERSIONPLIST="${SDKROOT}/System/Library/CoreServices/SystemVersion.plist"
VERSIONKEY="ProductBuildVersion"
BUILDVERSION=`grep -A 1 ${VERSIONKEY} ${VERSIONPLIST} | grep -v ${VERSIONKEY} | sed 's|.*<string>\(.*\)</string>.*|\1|g'`

echo "Building ${TARGET} version ${SQLITE_VERSION} for ${RELEASE} using ${BUILDVERSION}"
if [ "${BUILD_CMD}" == "smartBuildit" ]; then
  echo sudo ~luna/bin/smartBuildit ${BUILDIT_ARGS}
  sudo ~luna/bin/smartBuildit ${BUILDIT_ARGS}
else
  echo sudo ~rc/bin/buildit ${BUILDIT_ARGS}
  sudo ~rc/bin/buildit ${BUILDIT_ARGS}
fi


ROOTBALL="${TARGET}_${SQLITE_VERSION}_${DATESTAMP}_${BUILDVERSION}.tgz"
SROOTBALL="${TARGET}-XX-${DATESTAMP}-${BUILDVERSION}.tgz"

ROOTDST="${ROOTBASE}/${PROJECT}~dst"

if [ -e "${ROOTDST}" ]; then
  echo "Creating tarball from root in destination dir: ${ROOTDST}"

  if [ ${ROOTPKGSTYLE} == "BINARIES" ]; then
    echo "( cd ${ROOTDST}; tar -cvzp -f /tmp/${ROOTBALL} --exclude='*local*' --exclude='*share*' --exclude='*include*' . )"
    ( cd ${ROOTDST}; tar -cvz -f /tmp/${ROOTBALL} --exclude='*local*' --exclude='*share*' --exclude='*include*' . )
  else
    echo "( cd ${ROOTDST}; tar -cvzp -f /tmp/${ROOTBALL} . )"
    ( cd ${ROOTDST}; tar -cvz -f /tmp/${ROOTBALL} . )
  fi


  echo "---------------------"
  echo "  sudo -u xdesign cp /tmp/${ROOTBALL} ~xdesign/roots/sqlite/${ROOTBALL}"
  echo ""
  if [ "$@ " == "-embedded " ]; then
    echo "  rsync -av --exclude='*local*' --exclude='*share*' --exclude='*include*' ${ROOTDST}/ rsync://root@localhost:10873/root/"
  fi

fi

