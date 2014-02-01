# buildroot.sh
#
# Performs B&I style quad-fat build of sqlite and creates a well-named root
# echos recommended commands to copy the root to the standard xdesign root folder
#
# Usage: . buildroot.sh
# 
# buildroot.sh should only be invoked from the folder it lives in
#

sudo ~rc/bin/buildit . -arch ppc -arch ppc64 -arch i386 -arch x86_64 -release Leopard

DATESTAMP=`date "+%Y%m%d"`

BUILDVERSION=`grep -A 1 ProductBuildVersion /System/Library/CoreServices/SystemVersion.plist | grep -v ProductBuildVersion | sed 's|</*string>||g' | awk '{ print $1 }'`

ROOTBALL="SQLite33_${DATESTAMP}_${BUILDVERSION}_quadfat.tgz"
SROOTBALL="SQLite-XX-${DATESTAMP}-${BUILDVERSION}-quadfat.tgz"

LEAFDIR=`pwd | sed 's|.*/||g'`
ROOTDST="/tmp/${LEAFDIR}.roots/${LEAFDIR}~dst"

echo "Creating tarball from root in destination dir: ${ROOTDST}"

echo "(cd ${ROOTDST}; tar cvzpf /tmp/$ROOTBALL .)"
(cd ${ROOTDST}; tar cvzpf /tmp/$ROOTBALL .)

echo "---------------------"
echo "To copy a NON-SUBMITTED root to xdesign's sqlite root folder, issue the following command:"
echo ""
echo "  sudo -u xdesign cp /tmp/$ROOTBALL ~xdesign/roots/sqlite/$ROOTBALL"
echo ""
echo "To copy a SUBMITTED root to xdesign's submitted sqlite root folder, issue the following command and put the submission number into the root tar ball in place of the 'XX'"
echo ""
echo "  sudo -u xdesign cp /tmp/$ROOTBALL ~xdesign/roots/sqlite/submission/$SROOTBALL"
echo ""
