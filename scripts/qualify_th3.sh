# qualify_th3.sh
#
# Runs the full set of unit tests and generates qualification reports for sqlite prior to submission
#
# Usage: . qualify_th3.sh [-dos] [-hfs] [-smb] [-afp] [-nfs] [-x86_64] [-i386] [-ppc] [-tmp]
# 
# qualify_th3.sh should only be invoked from the root project folder (. scripts/qualify_th3.sh)
#

OPT_ON="1"
OPT_OFF="0"
REMOTE_HOST="aswift.apple.com"
REMOTE_USER="local"
REMOTE_PASSWORD="local"
REMOTE_QUALIFICATION_DIR="QUALIFICATION"
LOCAL_HOSTNAME=`hostname -s`

REBUILD=${OPT_ON}
DO_BUILD=${OPT_ON}
CANCEL=${OPT_OFF}

TEST_HFS=${OPT_OFF}
TEST_AFP=${OPT_OFF}
TEST_SMB=${OPT_OFF}
TEST_NFS=${OPT_OFF}
TEST_DOS=${OPT_OFF}
TEST_X86_64=${OPT_OFF}
TEST_I386=${OPT_OFF}
TEST_PPC=${OPT_OFF}

# test harness specifics
HARNESS="TH3"
TESTNAME="th3_fulltest"
TESTNAME_QUICK="th3_quicktest"
TESTNAME_FULL="th3_fulltest"

# by default run all tests, if specific tests are requested then just run those
TEST_ALL_FS=${OPT_ON}
TEST_ALL_ARCH=${OPT_ON}
PROXY_CONFIGS="${OPT_ON} ${OPT_OFF}"
SHOWHELP=${OPT_OFF}

for OPTARG in $@; do
    OPT=0
    if [ "${OPTARG}" == "-dos" ] ; then OPT=1; TEST_DOS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-hfs" ] ; then OPT=1; TEST_HFS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-smb" ] ; then OPT=1; TEST_SMB=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-afp" ] ; then OPT=1; TEST_AFP=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-nfs" ] ; then OPT=1; TEST_NFS=${OPT_ON}; TEST_ALL_FS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-x86_64" ] ; then OPT=1; TEST_X86_64=${OPT_ON}; TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-i386"   ] ; then OPT=1; TEST_I386=${OPT_ON};   TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-ppc"    ] ; then OPT=1; TEST_PPC=${OPT_ON};    TEST_ALL_ARCH=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-tmp" ] ; then OPT=1; QUALIFICATION_DIR="/tmp/sqlite_q"; fi
    if [ "${OPTARG}" == "-full" ] ; then OPT=1; TESTNAME=${TESTNAME_FULL}; fi
    if [ "${OPTARG}" == "-quick" ] ; then OPT=1; TESTNAME=${TESTNAME_QUICK}; fi
    if [ "${OPTARG}" == "-nobuild" ] ; then OPT=1; REBUILD=${OPT_OFF}; fi    
    if [ "${OPTARG}" == "-notest" ] ; then OPT=1; TEST_ALL_FS=${OPT_OFF}; fi    
    if [ "${OPTARG}" == "-proxy" ] ; then OPT=1; PROXY_CONFIGS=${OPT_ON}; fi
    if [ "${OPTARG}" == "-noproxy" ] ; then OPT=1; PROXY_CONFIGS=${OPT_OFF}; fi
    if [ "${OPTARG}" == "-help" ] ; then OPT=1; SHOWHELP=${OPT_ON}; fi
    if [ ${OPT} == 0 ] ; then TESTNAME=${OPTARG}; fi
done

if [ ${SHOWHELP} == ${OPT_ON} ]; then
  TEST_ALL_FS=${OPT_OFF}; 
  TEST_ALL_ARCH=${OPT_OFF};
  DO_BUILD=${OPT_OFF};
  echo "Usage: bash scripts/qualify_th3.sh [options] [testname]"
  echo "  file system options:"
  echo "    -hfs -smb -afp -nfs -dos"
  echo "  arch options:"
  echo "    -x86_64 -i386 -ppc"
  echo "  other options:"
  echo "    -tmp             Use /tmp/sqlite_q for tests and logs on local system"
  echo "    -full            Run the full ${HARNESS} suite ('veryquick' is default)"
  echo "    -quick           Run the quick ${HARNESS} suite ('veryquick' is default)"
  echo "    -nobuild         Don't rebuild the ${HARNESS} if it already exists"
  echo "    -notest          Don't run any tests, just build the test fixture"
  echo "    -proxy           Run only the proxy variant on network file systems"
  echo "    -noproxy         Run only the non-proxy variant on network file systems"

else

  if [ ${TEST_ALL_FS} == ${OPT_ON} ]; then
      TEST_HFS=${OPT_ON}; TEST_AFP=${OPT_ON}; TEST_SMB=${OPT_ON}; TEST_NFS=${OPT_ON}; TEST_DOS=${OPT_ON}
  fi

  if [ ${TEST_ALL_ARCH} == ${OPT_ON} ]; then
      TEST_X86_64=${OPT_ON}; TEST_I386=${OPT_ON};
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

  if [ "${QUALIFICATION_DIR} " == " " ]; then
      QUALDIR="/Volumes/Data/Testing/sqlite_${HARNESS}_q";
  else
      QUALDIR="${QUALIFICATION_DIR}"
  fi
  MOUNTS="/tmp/sqlite_qmnts"
  SRCROOT=`pwd`
  BUILDDIR="${QUALDIR}/build"
  BUILDLOG="${QUALDIR}/build.log"

  QUALLOGDIR="${QUALDIR}/logs"
  STARTTIME=`date`

  if [ -d ${QUALLOGDIR} ]; then
    if [ -f ${QUALLOGDIR}/STARTTIME ]; then
      PREVSTART=`cat ${QUALLOGDIR}/STARTTIME`
      echo "Moving previous unit test results to ${QUALLOGDIR}.${PREVSTART}"
      mv ${QUALLOGDIR} ${QUALLOGDIR}.${PREVSTART} 
    else
      echo "Moving previous unit test results to ${QUALLOGDIR}.prev"
      rm -rf ${QUALLOGDIR}.prev
      mv ${QUALLOGDIR} ${QUALLOGDIR}.prev 
    fi
    echo ""
  fi

  echo "Starting qualification tests for sqlite in ${QUALDIR} at ${STARTTIME}"
  echo "  Archs: " ${TEST_ARCHS}
  echo -n "  File systems: " 
  if [ ${TEST_HFS} == ${OPT_ON} ] ; then echo -n "HFS " ; fi ;
  if [ ${TEST_AFP} == ${OPT_ON} ] ; then echo -n "AFP " ; fi ;
  if [ ${TEST_SMB} == ${OPT_ON} ] ; then echo -n "SMB " ; fi ;
  if [ ${TEST_NFS} == ${OPT_ON} ] ; then echo -n "NFS " ; fi ;
  if [ ${TEST_DOS} == ${OPT_ON} ] ; then echo -n "DOS " ; fi ;
  echo ""
  echo "  Test: " ${TESTNAME}

  mkdir -p ${QUALLOGDIR}
  if [ ! -d ${MOUNTS} ] ; then 
    mkdir -p ${MOUNTS}
  fi

fi

##
## Build the test harness
##
TARGET=${TESTNAME}
TESTEXECUTABLE="${BUILDDIR}/${TARGET}_sym/Release/${TARGET}"
if [ -x ${TESTEXECUTABLE} ]; then
  DO_BUILD=${REBUILD}
fi

if [ ${DO_BUILD} == ${OPT_ON} ]; then

  if [ -d ${BUILDDIR} ]; then
    echo "Moving previous build and log to ${BUILDDIR}.prev and ${BUILDLOG}.prev"
    rm -rf ${BUILDDIR}.prev
    mv ${BUILDDIR} ${BUILDDIR}.prev 
    rm -rf ${BUILDLOG}.prev
    mv ${BUILDLOG} ${BUILDLOG}.prev 
    echo ""
  fi
  
  echo -n "Building ${TARGET} in ${BUILDDIR}..."
  mkdir -p ${BUILDDIR} 
  ( \
    xcodebuild -target ${TARGET} -configuration Release OBJROOT=${BUILDDIR}/${TARGET}_obj SYMROOT=${BUILDDIR}/${TARGET}_sym build > ${BUILDLOG} 2>&1; \
  )

  if [ -x ${TESTEXECUTABLE} ]; then
    echo "done (${BUILDLOG})";
    CANCEL=${OPT_OFF}
  else
    echo "failed (missing executable) (${BUILDLOG})"
    CANCEL=${OPT_ON}
    echo "---------------------------------------------------------"
    tail ${BUILDLOG}
  fi

fi

SQLITE_VERSION=`cat public_source/VERSION`
echo "th3 based on sqlite version ${SQLITE_VERSION}"
echo "--------------------------------------------------------------------------------"

TESTARGS=""
GREP_ERRORLINE1="errors out of"
GREP_ERRORLINE2=""

##
## HFS tests
##
if [ ${CANCEL} == ${OPT_OFF} -a ${TEST_HFS} == ${OPT_ON} ] ; then 

  HFSTESTDIR=${MOUNTS}/hfs_tests
  rm -rf ${HFSTESTDIR}
  mkdir ${HFSTESTDIR}
  
  for PROXY in ${PROXY_CONFIGS} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      if [ $CANCEL == ${OPT_OFF} ]; then 
        echo -n "HFS${PROXY_STATUS} $ARCH unit test... "
        TESTDIR=${HFSTESTDIR}/test_hfs${PROXY_STATUS}_${ARCH}
        TESTLOG=${QUALLOGDIR}/test_hfs${PROXY_STATUS}_${ARCH}.log
        mkdir ${TESTDIR}
        if [ $? == "1" ]; then
          echo "Failed to create ${TESTDIR}, skipping (0 errors out of 0 tests)" > ${TESTLOG}
        else
          ( cd ${TESTDIR}; arch -${ARCH} ${TESTEXECUTABLE} ${TESTARGS} > ${TESTLOG} 2>&1; )
          if [ $? == "130" ]; then
            echo "Tests interrupted, exiting...";
            CANCEL=${OPT_ON}
          fi
        fi
        echo "done (${TESTLOG})"
        if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
        if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
        echo ""
      fi
    done

  done

fi

##
## AFP tests
##
if [ ${CANCEL} == ${OPT_OFF} -a ${TEST_AFP} == ${OPT_ON} ] ; then 

  AFPHOST=${REMOTE_HOST}
  AFPUSER=${REMOTE_USER}
  AFPPASS=${REMOTE_PASSWORD}
  AFPHOSTDIR=${REMOTE_QUALIFICATION_DIR}
  AFPMNT=${MOUNTS}/afpmnt
  AFPTESTDIR=${AFPMNT}/${LOCAL_HOSTNAME}/afp_tests

  if [ ! -d ${AFPMNT} ] ; then 
    mkdir ${AFPMNT}
  fi
  mount_afp afp://${AFPUSER}:${AFPPASS}@${AFPHOST}/${AFPHOSTDIR} ${AFPMNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping file system"
    fi
  else
    rm -rf ${AFPTESTDIR}
    mkdir -p ${AFPTESTDIR}

    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 

          echo -n "AFP${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${AFPTESTDIR}/test_afp${PROXY_STATUS}_${ARCH}
          TESTLOG=${QUALLOGDIR}/test_afp${PROXY_STATUS}_${ARCH}.log
          mkdir ${TESTDIR}
          if [ $? == "1" ]; then
            echo "Failed to create ${TESTDIR}, skipping (0 errors out of 0 tests)" > ${TESTLOG}
          else
            ( cd ${TESTDIR}; arch -${ARCH} ${TESTEXECUTABLE} ${TESTARGS} > ${TESTLOG} 2>&1; )
          fi
          if [ $? == "130" ]; then
            echo "Tests interrupted, exiting...";
            CANCEL=${OPT_ON}
          fi

          echo "done (${TESTLOG})"
          if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
          if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
          echo ""
        fi
      done
    done
  
    umount ${AFPMNT}
  fi
fi

##
## SMB tests
##
if [ ${CANCEL} == ${OPT_OFF} -a $TEST_SMB == ${OPT_ON} ] ; then 

  SMBHOST=${REMOTE_HOST}
  SMBUSER=${REMOTE_USER}
  SMBPASS=${REMOTE_PASSWORD}
  SMBHOSTDIR=${REMOTE_QUALIFICATION_DIR}
  SMBMNT=${MOUNTS}/smbmnt
  SMBTESTDIR=${SMBMNT}/${LOCAL_HOSTNAME}/smb_tests

  if [ ! -d ${SMBMNT} ] ; then 
    mkdir ${SMBMNT}
  fi
  mount_smbfs //${SMBUSER}:${SMBPASS}@${SMBHOST}/${SMBHOSTDIR} ${SMBMNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping file system"
    fi
  else
    rm -rf ${SMBTESTDIR}
    mkdir -p ${SMBTESTDIR}

    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "SMB${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${SMBTESTDIR}/test_smb${PROXY_STATUS}_${ARCH}
          TESTLOG=${QUALLOGDIR}/test_smb${PROXY_STATUS}_${ARCH}.log
          mkdir ${TESTDIR}
          if [ $? == "1" ]; then
            echo "Failed to create ${TESTDIR}, skipping (0 errors out of 0 tests)" > ${TESTLOG}
          else
            ( cd ${TESTDIR}; arch -${ARCH} ${TESTEXECUTABLE} ${TESTARGS} > ${TESTLOG} 2>&1; )
          fi
          if [ $? == "130" ]; then
            echo "Tests interrupted, exiting...";
            CANCEL=${OPT_ON}
          fi
          echo "done (${TESTLOG})"
          if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
          if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
          echo ""
        fi
      done
    done

    umount ${SMBMNT}
  fi
fi

##
## NFS tests
##
if [ ${CANCEL} == ${OPT_OFF} -a ${TEST_NFS} == ${OPT_ON} ] ; then 

  NFSHOST=${REMOTE_HOST}
  NFSUSER=${REMOTE_USER}
  NFSHOSTDIR=/Volumes/Data/Testing/${REMOTE_QUALIFICATION_DIR}
  NFSMNT=${MOUNTS}/nfsmnt
  NFSTESTDIR=${NFSMNT}/${LOCAL_HOSTNAME}/nfs_tests

  if [ ! -d ${NFSMNT} ] ; then 
    mkdir ${NFSMNT}
  fi

  ssh ${NFSUSER}@${NFSHOST} "( chmod a+rw ${NFSHOSTDIR} ; mkdir -p ${NFSHOSTDIR}/${LOCAL_HOSTNAME}; chmod a+rw ${NFSHOSTDIR}/${LOCAL_HOSTNAME} )"

  mount_nfs ${NFSHOST}:${NFSHOSTDIR} ${NFSMNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping file system"
    fi
  else
    rm -rf ${NFSTESTDIR}
    mkdir -p ${NFSTESTDIR}

    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "NFS${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${NFSTESTDIR}/test_nfs${PROXY_STATUS}_${ARCH}
          TESTLOG=${QUALLOGDIR}/test_nfs${PROXY_STATUS}_${ARCH}.log
          mkdir ${TESTDIR}
          if [ $? == "1" ]; then
            echo "Failed to create ${TESTDIR}, skipping (0 errors out of 0 tests)" > ${TESTLOG}
          else
            ( cd ${TESTDIR}; arch -${ARCH} ${TESTEXECUTABLE} ${TESTARGS} > ${TESTLOG} 2>&1; )
          fi
          if [ $? == "130" ]; then
            echo "Tests interrupted, exiting...";
            CANCEL=${OPT_ON}
          fi
          echo "done (${TESTLOG})"
          if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
          if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
          echo ""
        fi
      done
    done

    umount ${NFSMNT}
  fi
fi

##
## DOS tests
##
DOSFSNAME=dosfs
DOSMNT=/Volumes/${DOSFSNAME}
DOSFSIMAGE=${MOUNTS}/dosfs.dmg
DOSTESTDIR=${DOSMNT}/dos_tests
if [  ${CANCEL} == ${OPT_OFF} -a ${TEST_DOS} == ${OPT_ON} ] ; then 

  if [ ! -f ${DOSFSIMAGE} ] ; then
    hdiutil create -megabytes 100 -fs MS-DOS ${DOSFSIMAGE} -volname ${DOSFSNAME}
    ERR=$?
    if [ ${ERR} != "0" ]; then
      if [ ${ERR} == "130" ]; then
        echo "Tests interrupted, exiting...";
        CANCEL=${OPT_ON}
      else
        echo "Create dos image failed, skipping file system"
      fi
    fi
  fi
  if [ ${CANCEL} == ${OPT_OFF} -a -f ${DOSFSIMAGE} ]; then
    if [ ! -d ${DOSMNT} ] ; then
      hdiutil attach ${DOSFSIMAGE}
      ERR=$?
      if [ ! -d ${DOSMNT} ]; then
        if [ ${ERR} == "130" ]; then
          echo "Tests interrupted, exiting...";
          CANCEL=${OPT_ON}
        else
          echo "Mount failed, skipping file system"
        fi
      fi
    fi
  fi
  if [ ${CANCEL} == ${OPT_OFF} -a -d ${DOSMNT} ]; then
    rm -rf ${DOSTESTDIR}
    mkdir ${DOSTESTDIR}

    for PROXY in 0 ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "DOS${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${DOSTESTDIR}/test_dos${PROXY_STATUS}_${ARCH}
          TESTLOG=${QUALLOGDIR}/test_dos${PROXY_STATUS}_${ARCH}.log
          mkdir ${TESTDIR}
          if [ $? == "1" ]; then
            echo "Failed to create ${TESTDIR}, skipping (0 errors out of 0 tests)" > ${TESTLOG}
          else
            ( cd ${TESTDIR}; arch -${ARCH} ${TESTEXECUTABLE} ${TESTARGS} > ${TESTLOG} 2>&1; )
          fi
          if [ $? == "130" ]; then
            echo "Tests interrupted, exiting...";
            CANCEL=${OPT_ON}
          fi
          echo "done (${TESTLOG})"
          if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
          if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
          echo ""
        fi
      done
    done

    hdiutil detach ${DOSMNT}
  fi
fi

##
## Unit test report summary
##
if [ ${CANCEL} == ${OPT_OFF} ] ; then 

  echo "--------------------------------------------------------------------------------"
  ENDTIME=`date`
  OSVERSION=`sw_vers -buildVersion`
  echo "Unit test qualification completed" > ${QUALLOGDIR}/summary.txt
  echo "Testfixture based on sqlite version ${SQLITE_VERSION}" >> ${QUALLOGDIR}/summary.txt
  echo "Qualification performed on system version ${OSVERSION}" >> ${QUALLOGDIR}/summary.txt
  echo "  Started   ${STARTTIME}" >> ${QUALLOGDIR}/summary.txt
  echo "  Completed ${ENDTIME}" >> ${QUALLOGDIR}/summary.txt
  echo "" >> ${QUALLOGDIR}/summary.txt
  ALLTESTLOGS=`(cd ${QUALLOGDIR}; ls test_*.log)`
  for TESTLOG in ${ALLTESTLOGS}; do
    LOGNAME=`echo ${TESTLOG} | sed 's/test_\(.*\)\.log/\1/'`
    FAILURES=`grep 'FAILED' ${QUALLOGDIR}/${TESTLOG}`
    ERRORS=`grep 'errors out of' ${QUALLOGDIR}/${TESTLOG}`
    echo "${LOGNAME}: ${ERRORS}" >> ${QUALLOGDIR}/summary.txt
    if [ "${FAILURES} " != " " ]; then 
      echo "  ${FAILURES}" >> ${QUALLOGDIR}/summary.txt
    fi
    grep -B 2 'Abort trap' ${QUALLOGDIR}/${TESTLOG} >> ${QUALLOGDIR}/summary.txt
    echo "" >> ${QUALLOGDIR}/summary.txt
  done

  cp ${QUALLOGDIR}/summary.txt ~/tmp/
  cat ${QUALLOGDIR}/summary.txt

fi
