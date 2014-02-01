# qualify.sh
#
# Runs the full set of unit tests and generates qualification reports for sqlite prior to submission
#
# Usage: . qualify.sh
# 
# qualify.sh should only be invoked from the root project folder (. scripts/qualify.sh)
#

OPT_ON="1"
OPT_OFF="0"

TEST_HFS=${OPT_OFF}
TEST_AFP=${OPT_OFF}
TEST_SMB=${OPT_OFF}
TEST_NFS=${OPT_OFF}
TEST_DOS=${OPT_OFF}
TEST_X86_64=${OPT_OFF}
TEST_I386=${OPT_OFF}
TEST_PPC=${OPT_OFF}

# by default run all tests, if specific tests are requested then just run those
TEST_ALL_FS=${OPT_ON}
TEST_ALL_ARCH=${OPT_ON}

for OPTARG in $@; do
    if [ "${OPTARG}" == "-dos" ] ; then TEST_DOS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-hfs" ] ; then TEST_HFS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-smb" ] ; then TEST_SMB=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-afp" ] ; then TEST_AFP=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-nfs" ] ; then TEST_NFS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-x86_64" ] ; then TEST_X86_64=${OPT_ON}; TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-i386"   ] ; then TEST_I386=${OPT_ON};   TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-ppc"    ] ; then TEST_PPC=${OPT_ON};    TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-tmp" ] ; then QUALDIR="/tmp/sqlite_q"; fi
done

if [ ${TEST_ALL_FS} == ${OPT_ON} ]; then
    TEST_HFS=${OPT_ON}; TEST_AFP=${OPT_ON}; TEST_SMB=${OPT_ON}; TEST_NFS=${OPT_ON}; TEST_DOS=${OPT_ON}
fi

if [ ${TEST_ALL_ARCH} == ${OPT_ON} ]; then
    TEST_X86_64=${OPT_ON}; TEST_I386=${OPT_ON}; TEST_PPC=${OPT_ON};
fi

TEST_ARCHS=""
ARCHS_FLAGS=""
if [ ${TEST_X86_64} == ${OPT_ON} ]; then TEST_ARCHS="${TEST_ARCHS} x86_64"; fi
if [ ${TEST_I386} == ${OPT_ON} ]; then TEST_ARCHS="${TEST_ARCHS} i386"; fi
if [ ${TEST_PPC} == ${OPT_ON} ]; then
    # check for ppc support
    if [ "`arch -ppc /bin/echo rosetta_installed` " == "rosetta_installed " ] ; then
        TEST_ARCHS="${TEST_ARCHS} ppc"
    else
        echo "Can't test ppc binaries";
    fi
fi
for ARCH in $TEST_ARCHS; do 
  ARCHS_FLAGS="${ARCHS_FLAGS} -arch ${ARCH}"
done

##
## Run qualification tests
##

if [ "${QUALDIR} " == " " ]; then
    QUALDIR="/Volumes/Data/Testing/sqlite_q";
fi
MOUNTS="/tmp/sqlite_qmnts"
SRCROOT=`pwd`
BUILDDIR="${QUALDIR}/build"
BUILDLOG="${QUALDIR}/build.log"

STARTTIME=`date`

if [ -d ${QUALDIR} ]; then
  echo "Moving previous build and unit test results to ${QUALDIR}.prev"
  rm -rf ${QUALDIR}.prev
  mv ${QUALDIR} ${QUALDIR}.prev 
  echo ""
fi

echo "Starting qualification tests for sqlite in ${QUALDIR} at ${STARTTIME}"
echo "  Archs: " $TEST_ARCHS
echo -n "  File systems: " 
if [ ${TEST_HFS} == ${OPT_ON} ] ; then echo -n "HFS " ; fi ;
if [ ${TEST_AFP} == ${OPT_ON} ] ; then echo -n "AFP " ; fi ;
if [ ${TEST_SMB} == ${OPT_ON} ] ; then echo -n "SMB " ; fi ;
if [ ${TEST_NFS} == ${OPT_ON} ] ; then echo -n "NFS " ; fi ;
if [ ${TEST_DOS} == ${OPT_ON} ] ; then echo -n "DOS " ; fi ;
echo ""

mkdir $QUALDIR
if [ ! -d ${MOUNTS} ] ; then 
  mkdir ${MOUNTS}
fi


##
## Build the fat testfixture
##
echo -n "Building sqlite for ${TEST_ARCHS} in ${BUILDDIR}..."
mkdir ${BUILDDIR} 
( \
  cd ${BUILDDIR}; \
  python ${SRCROOT}/scripts/configure.py -preferproxy -cflags="${ARCHS_FLAGS}" > ${BUILDLOG} 2>&1; \
  make testfixture >> ${BUILDLOG} 2>&1; \
)
echo "done (${BUILDLOG})";

export DYLD_LIBRARY_PATH=${BUILDDIR}/.libs
TESTFIXTURE="${BUILDDIR}/testfixture"
TESTARGS="${SRCROOT}/public_source/test/veryquick.test"

##
## HFS tests
##
if [ $TEST_HFS == ${OPT_ON} ] ; then 

  HFSTESTDIR=${MOUNTS}/hfs_tests
  rm -rf ${HFSTESTDIR}
  mkdir ${HFSTESTDIR}
  
  for PROXY in ${OPT_OFF} ${OPT_ON} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      echo -n "HFS${PROXY_STATUS} $ARCH unit test... "
      TESTDIR=${HFSTESTDIR}/test_hfs${PROXY_STATUS}_${ARCH}
      TESTLOG=${QUALDIR}/test_hfs${PROXY_STATUS}_${ARCH}.log
      mkdir ${TESTDIR}
      ( cd ${TESTDIR}; arch -${ARCH} ${TESTFIXTURE} ${TESTARGS} > ${TESTLOG} 2>&1; )
      echo "done (${TESTLOG})"
      grep "Failures on these tests:" ${TESTLOG} 
      grep "0 errors out of" ${TESTLOG}
      echo ""
    done

  done

fi

if [ $TEST_AFP == ${OPT_ON} -o $TEST_SMB == ${OPT_ON} -o $TEST_NFS == ${OPT_ON} ] ; then
  # all required hosts should awoken
  WAKEHOSTS="sqlitetest@aswift.apple.com"
  for WAKEHOST in ${WAKEHOSTS} ; do
    wakessh ${WAKEHOST} echo "Sending wakeup to ${WAKEHOST}"
  done
fi

##
## AFP tests
##
if [ $TEST_AFP == ${OPT_ON} ] ; then 

  AFPHOST="aswift.apple.com"
  AFPUSER="sqlitetest"
  AFPMNT=${MOUNTS}/afpmnt
  AFPTESTDIR=${AFPMNT}/afp_tests

  if [ ! -d ${AFPMNT} ] ; then 
    mkdir ${AFPMNT}
  fi
  mount_afp afp://${AFPUSER}:${AFPUSER}@${AFPHOST}/${AFPUSER} ${AFPMNT}
  rm -rf ${AFPTESTDIR}
  mkdir ${AFPTESTDIR}

  for PROXY in ${OPT_OFF} ${OPT_ON} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      echo -n "AFP${PROXY_STATUS} $ARCH unit test... "
      TESTDIR=${AFPTESTDIR}/test_afp${PROXY_STATUS}_${ARCH}
      TESTLOG=${QUALDIR}/test_afp${PROXY_STATUS}_${ARCH}.log
      mkdir ${TESTDIR}
      ( cd ${TESTDIR}; arch -${ARCH} ${TESTFIXTURE} ${TESTARGS} > ${TESTLOG} 2>&1; )
      echo "done (${TESTLOG})"
      grep "Failures on these tests:" ${TESTLOG} 
      grep "0 errors out of" ${TESTLOG}
      echo ""
    done
  done

  umount ${AFPMNT}
  
fi

##
## SMB tests
##
if [ $TEST_SMB == ${OPT_ON} ] ; then 

  SMBHOST="aswift.apple.com"
  SMBUSER="sqlitetest"
  SMBMNT=${MOUNTS}/smbmnt
  SMBTESTDIR=${SMBMNT}/smb_tests

  if [ ! -d ${SMBMNT} ] ; then 
    mkdir ${SMBMNT}
  fi
  mount_smbfs //${SMBUSER}:${SMBUSER}@${SMBHOST}/${SMBUSER} ${SMBMNT}
  rm -rf ${SMBTESTDIR}
  mkdir ${SMBTESTDIR}

  for PROXY in ${OPT_OFF} ${OPT_ON} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      echo -n "SMB${PROXY_STATUS} $ARCH unit test... "
      TESTDIR=${SMBTESTDIR}/test_smb${PROXY_STATUS}_${ARCH}
      TESTLOG=${QUALDIR}/test_smb${PROXY_STATUS}_${ARCH}.log
      mkdir ${TESTDIR}
      ( cd ${TESTDIR}; arch -${ARCH} ${TESTFIXTURE} ${TESTARGS} > ${TESTLOG} 2>&1; )
      echo "done (${TESTLOG})"
      grep "Failures on these tests:" ${TESTLOG} 
      grep "0 errors out of" ${TESTLOG}
      echo ""
    done
  done

  umount ${SMBMNT}
  
fi

##
## NFS tests
##
if [ $TEST_NFS == ${OPT_ON} ] ; then 

  NFSHOST="aswift.apple.com"
  NFSMNT=${MOUNTS}/nfsmnt
  NFSTESTDIR=${NFSMNT}/Testing/nfs_tests

  if [ ! -d ${NFSMNT} ] ; then 
    mkdir ${NFSMNT}
  fi
  mount_nfs ${NFSHOST}:/Volumes/Data ${NFSMNT}
  rm -rf ${NFSTESTDIR}
  mkdir ${NFSTESTDIR}

  for PROXY in ${OPT_OFF} ${OPT_ON} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      echo -n "NFS${PROXY_STATUS} $ARCH unit test... "
      TESTDIR=${NFSTESTDIR}/test_nfs${PROXY_STATUS}_${ARCH}
      TESTLOG=${QUALDIR}/test_nfs${PROXY_STATUS}_${ARCH}.log
      mkdir ${TESTDIR}
      ( cd ${TESTDIR}; arch -${ARCH} ${TESTFIXTURE} ${TESTARGS} > ${TESTLOG} 2>&1; )
      echo "done (${TESTLOG})"
      grep "Failures on these tests:" ${TESTLOG} 
      grep "0 errors out of" ${TESTLOG}
      echo ""
    done
  done

  umount ${NFSMNT}
  
fi


##
## DOS tests
##
DOSFSNAME=dosfs
DOSMNT=/Volumes/${DOSFSNAME}
DOSFSIMAGE=${MOUNTS}/dosfs.dmg
DOSTESTDIR=${DOSMNT}/dos_tests

if [ $TEST_DOS == ${OPT_ON} ] ; then 

  if [ ! -f ${DOSFSIMAGE} ] ; then
    hdiutil create -megabytes 100 -fs MS-DOS ${DOSFSIMAGE} -volname ${DOSFSNAME}
  fi
  if [ ! -d ${DOSMNT} ] ; then
    hdiutil attach ${DOSFSIMAGE}
  fi
  rm -rf ${DOSTESTDIR}
  mkdir ${DOSTESTDIR}

  for PROXY in 0 ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      echo -n "DOS${PROXY_STATUS} $ARCH unit test... "
      TESTDIR=${DOSTESTDIR}/test_dos${PROXY_STATUS}_${ARCH}
      TESTLOG=${QUALDIR}/test_dos${PROXY_STATUS}_${ARCH}.log
      mkdir ${TESTDIR}
      ( cd ${TESTDIR}; arch -${ARCH} ${TESTFIXTURE} ${TESTARGS} > ${TESTLOG} 2>&1; )
      echo "done (${TESTLOG})"
      grep "Failures on these tests:" ${TESTLOG} 
      grep "0 errors out of" ${TESTLOG}
      echo ""
    done
  done

  umount ${DOSMNT}
  hdiutil detach ${DOSMNT}
  
fi

##
## Unit test report summary
## 
ENDTIME=`date`
echo "Unit test qualification completed started" > ${QUALDIR}/summary.txt
echo "  Started   ${STARTTIME}"
echo "  Completed ${ENDTIME}"
echo ""
ALLTESTLOGS=`(cd ${QUALDIR}; ls test_*.log)`
for TESTLOG in ${ALLTESTLOGS}; do
  LOGNAME=`echo ${TESTLOG} | sed 's/test_\(.*\)\.log/\1/'`
  FAILURES=`grep 'Failures on these tests' ${QUALDIR}/${TESTLOG} | sed 's/Failures on these tests://'`
  ERRORS=`grep 'errors out of' ${QUALDIR}/${TESTLOG}`
  echo "${LOGNAME}: ${ERRORS}" >> ${QUALDIR}/summary.txt
  if [ "${FAILURES} " != " " ]; then 
    echo "  ${FAILURES}" >> ${QUALDIR}/summary.txt
  fi
  grep -B 2 'Abort trap' ${QUALDIR}/${TESTLOG} >> ${QUALDIR}/summary.txt
  echo "" >> ${QUALDIR}/summary.txt
done

cp ${QUALDIR}/summary.txt ~/tmp/
cat ${QUALDIR}/summary.txt
