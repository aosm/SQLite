# qualify.sh
#
# Runs the full set of unit tests and generates qualification reports for sqlite prior to submission
#
# Usage: . qualify.sh [-th3] [-dos] [-hfs] [-smb] [-afp] [-nfs] [-x86_64] [-i386] [-ppc] [-tmp]
# 
# qualify.sh should only be invoked from the root project folder (. scripts/qualify.sh)
# 
# set QUALIFICATION_DIR to a custom directory if don't want to use the default:
#     /Volumes/Data/Testing/sqlite_${HARNESS}_q
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
HARNESS="testfixture"
TESTNAME=""
TESTNAME_QUICK="quick"
TESTNAME_FULL="all"
TARGET=testfixture
BUILDCONFIG="Debug"

# by default run all tests, if specific tests are requested then just run those
TEST_ALL_FS=${OPT_ON}
TEST_ALL_ARCH=${OPT_ON}
PROXY_CONFIGS="${OPT_ON} ${OPT_OFF}"
SHOWHELP=${OPT_OFF}
LOG_STDOUT=${OPT_OFF}

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
    if [ "${OPTARG}" == "-stdout" ] ; then OPT=1; LOG_STDOUT=${OPT_ON}; fi
    if [ "${OPTARG}" == "-th3" ] ; then OPT=1; HARNESS="TH3"; fi
    if [ ${OPT} == 0 ] ; then TESTNAME=${OPTARG}; fi
done

##
## Adjust settings for TH3
##
if [ ${HARNESS} == "TH3" ]; then
  if [ "${TESTNAME} " == " " -o "${TESTNAME} " == "${TESTNAME_FULL} " ]; then
    TESTNAME="th3_fulltest"
  fi
  if [ "${TESTNAME} " == "${TESTNAME_QUICK} " ]; then
    TESTNAME="th3_quicktest"
  fi
  TARGET=${TESTNAME}
  BUILDCONFIG="Release"
else
  if [ "${TESTNAME} " == " " ]; then
    TESTNAME="veryquick"
  fi
fi

if [ ${SHOWHELP} == ${OPT_ON} ]; then
  TEST_ALL_FS=${OPT_OFF}; 
  TEST_ALL_ARCH=${OPT_OFF};
  DO_BUILD=${OPT_OFF};
  CANCEL=${OPT_ON};
  echo "Usage: bash scripts/qualify.sh [options] [testname]"
  echo "  file system options:"
  echo "    -hfs -smb -afp -nfs -dos"
  echo "  arch options:"
  echo "    -x86_64 -i386 -ppc"
  echo "  other options:"
  echo "    -th3             Qualify using the TH3 test harness instead of the TCL based testfixture"
  echo "    -tmp             Use /tmp/sqlite_q for tests and logs on local system"
  echo "    -full            Run the full ${HARNESS} suite ('veryquick' is default)"
  echo "    -quick           Run the quick ${HARNESS} suite ('veryquick' is default)"
  echo "    -nobuild         Don't rebuild the ${HARNESS} if it already exists"
  echo "    -notest          Don't run any tests, just build the test harness"
  echo "    -proxy           Run only the proxy variant on network file systems"
  echo "    -noproxy         Run only the non-proxy variant on network file systems"
  echo "    -stdout          Log test output to STDOUT instead of creating log files"

else

  if [ ${TEST_ALL_FS} == ${OPT_ON} ]; then
    if [ ${HARNESS} == "TH3" ]; then
      TEST_HFS=${OPT_ON};
    else
      TEST_HFS=${OPT_ON}; TEST_AFP=${OPT_ON}; TEST_SMB=${OPT_ON}; TEST_NFS=${OPT_ON}; TEST_DOS=${OPT_ON}
    fi
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

  if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
    echo "Logging to STDOUT"
  else
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
    mkdir -p ${QUALLOGDIR}
    date "+%Y%m%d_%H%M%S" > ${QUALLOGDIR}/STARTTIME
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

  if [ ! -d ${MOUNTS} ] ; then 
    mkdir -p ${MOUNTS}
  fi

fi

 
##
## Build the test harness
##
TESTEXECUTABLE="${BUILDDIR}/${TARGET}_sym/${BUILDCONFIG}/${TARGET}"
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
    xcodebuild -target ${TARGET} -configuration ${BUILDCONFIG} OBJROOT=${BUILDDIR}/${TARGET}_obj SYMROOT=${BUILDDIR}/${TARGET}_sym build > ${BUILDLOG} 2>&1; \
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

if [ ${HARNESS} == "TH3" ]; then 
  SQLITE_VERSION=`cat public_source/VERSION`
  echo "th3 based on sqlite version ${SQLITE_VERSION}"
  echo "--------------------------------------------------------------------------------"

  TESTARGS=""
  GREP_ERRORLINE1="errors out of"
  GREP_ERRORLINE2=""
else
  SQLITE_VERSION=""
  if [ -x ${TESTEXECUTABLE} ]; then 
    VERSION_SCRIPT="${BUILDDIR}/v.test"
    echo 'sqlite3 db :memory:; set v [db version]; db close; puts $v' > ${BUILDDIR}/v.test
    SQLITE_VERSION=`${TESTEXECUTABLE} ${VERSION_SCRIPT}`
    echo "${TARGET} based on sqlite version ${SQLITE_VERSION}"
    echo "--------------------------------------------------------------------------------"
  fi

  TESTARGS="${SRCROOT}/public_source/test/${TESTNAME}.test"
  GREP_ERRORLINE1="Failures on these tests:"
  GREP_ERRORLINE2="0 errors out of"
fi


##
## HFS tests
##
if [ ${CANCEL} == ${OPT_OFF} -a ${TEST_HFS} == ${OPT_ON} ] ; then 

  TESTDIR=${MOUNTS}/hfs_tests
  FS_NAME=hfs
  rm -rf ${TESTDIR}
  mkdir ${TESTDIR}
  
  for PROXY in ${PROXY_CONFIGS} ; do
    export SQLITE_FORCE_PROXY_LOCKING=$PROXY
    export PROXY_STATUS=""
    if [ $PROXY == ${OPT_ON} ] ; then 
      export PROXY_STATUS="_proxy";
    fi
    for ARCH in $TEST_ARCHS ; do
      if [ $CANCEL == ${OPT_OFF} ]; then 
        echo -n "${FS_NAME}${PROXY_STATUS} $ARCH unit test... "
        TESTDIR=${TESTDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}
        if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
          TESTLOG="/dev/stdout"
        else
          TESTLOG="${QUALLOGDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}.log"
        fi
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
        if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
          if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
          if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
          echo ""
        fi
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
  FS_NAME=afp
  FS_MNT=${MOUNTS}/${FS_NAME}mnt
  TESTDIR=${FS_MNT}/${LOCAL_HOSTNAME}/${FS_NAME}_tests

  if [ ! -d ${FS_MNT} ] ; then 
    mkdir ${FS_MNT}
  fi
  echo "mount -t afp afp://${AFPUSER}:${AFPPASS}@${AFPHOST}/${AFPHOSTDIR} ${FS_MNT}"
  mount -t afp afp://${AFPUSER}:${AFPPASS}@${AFPHOST}/${AFPHOSTDIR} ${FS_MNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping ${FS_NAME} file system"
    fi
  else
    rm -rf ${TESTDIR}
    mkdir ${TESTDIR}
    
    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "${FS_NAME}${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${TESTDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}
          if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
            TESTLOG="/dev/stdout"
          else
            TESTLOG="${QUALLOGDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}.log"
          fi
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
          if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
            if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
            if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
            echo ""
          fi
        fi
      done

    done
  
    umount ${FS_MNT}
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
  FS_NAME=smb
  FS_MNT=${MOUNTS}/${FS_NAME}mnt
  TESTDIR=${FS_MNT}/${LOCAL_HOSTNAME}/${FS_NAME}_tests

  if [ ! -d ${FS_MNT} ] ; then 
    mkdir ${FS_MNT}
  fi
  echo "mount -t smbfs //${SMBUSER}:${SMBPASS}@${SMBHOST}/${SMBHOSTDIR} ${FS_MNT}"
  mount -t smbfs //${SMBUSER}:${SMBPASS}@${SMBHOST}/${SMBHOSTDIR} ${FS_MNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping ${FS_NAME} file system"
    fi
  else
    rm -rf ${TESTDIR}
    mkdir ${TESTDIR}
    
    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "${FS_NAME}${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${TESTDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}
          if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
            TESTLOG="/dev/stdout"
          else
            TESTLOG="${QUALLOGDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}.log"
          fi
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
          if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
            if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
            if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
            echo ""
          fi
        fi
      done

    done

    umount ${FS_MNT}
  fi
fi

##
## NFS tests
##
if [ ${CANCEL} == ${OPT_OFF} -a ${TEST_NFS} == ${OPT_ON} ] ; then 

  NFSHOST=${REMOTE_HOST}
  NFSUSER=${REMOTE_USER}
  NFSHOSTDIR=/Volumes/Data/Testing/${REMOTE_QUALIFICATION_DIR}
  FS_NAME=nfs
  FS_MNT=${MOUNTS}/${FS_NAME}mnt
  TESTDIR=${FS_MNT}/${LOCAL_HOSTNAME}/${FS_NAME}_tests

  ssh ${NFSUSER}@${NFSHOST} "( chmod a+rw ${NFSHOSTDIR} ; mkdir -p ${NFSHOSTDIR}/${LOCAL_HOSTNAME}; chmod a+rw ${NFSHOSTDIR}/${LOCAL_HOSTNAME} )"

  if [ ! -d ${FS_MNT} ] ; then 
    mkdir ${FS_MNT}
  fi
  echo "mount -t nfs ${NFSHOST}:${NFSHOSTDIR} ${FS_MNT}"
  mount -t nfs ${NFSHOST}:${NFSHOSTDIR} ${FS_MNT}
  ERR=$?
  if [ ${ERR} != "0" ]; then
    if [ ${ERR} == "130" ]; then
      echo "Tests interrupted, exiting...";
      CANCEL=${OPT_ON}
    else
      echo "Mount failed, skipping file system"
    fi
  else
    rm -rf ${TESTDIR}
    mkdir ${TESTDIR}
    
    for PROXY in ${PROXY_CONFIGS} ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "${FS_NAME}${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${TESTDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}
          if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
            TESTLOG="/dev/stdout"
          else
            TESTLOG="${QUALLOGDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}.log"
          fi
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
          if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
            if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
            if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
            echo ""
          fi
        fi
      done

    done

    umount ${FS_MNT}
  fi
fi

##
## DOS tests
##
FS_NAME=dos
FS_MNT=/Volumes/${FS_NAME}
DOSFSIMAGE=${MOUNTS}/${FS_NAME}.dmg
TESTDIR=${FS_MNT}/${FS_NAME}_tests
if [  ${CANCEL} == ${OPT_OFF} -a ${TEST_DOS} == ${OPT_ON} ] ; then 

  if [ ! -f ${DOSFSIMAGE} ] ; then
    hdiutil create -megabytes 100 -fs MS-DOS ${DOSFSIMAGE} -volname ${FS_NAME}
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
    if [ ! -d ${FS_MNT} ] ; then
      hdiutil attach ${DOSFSIMAGE}
      ERR=$?
      if [ ! -d ${FS_MNT} ]; then
        if [ ${ERR} == "130" ]; then
          echo "Tests interrupted, exiting...";
          CANCEL=${OPT_ON}
        else
          echo "Mount failed, skipping file system"
        fi
      fi
    fi
  fi
  if [ ${CANCEL} == ${OPT_OFF} -a -d ${FS_MNT} ]; then
    rm -rf ${TESTDIR}
    mkdir ${TESTDIR}
    
    for PROXY in 0 ; do
      export SQLITE_FORCE_PROXY_LOCKING=$PROXY
      export PROXY_STATUS=""
      if [ $PROXY == ${OPT_ON} ] ; then 
        export PROXY_STATUS="_proxy";
      fi
      for ARCH in $TEST_ARCHS ; do
        if [ $CANCEL == ${OPT_OFF} ]; then 
          echo -n "${FS_NAME}${PROXY_STATUS} $ARCH unit test... "
          TESTDIR=${TESTDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}
          if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
            TESTLOG="/dev/stdout"
          else
            TESTLOG="${QUALLOGDIR}/test_${FS_NAME}${PROXY_STATUS}_${ARCH}.log"
          fi
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
          if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
            if [ "${GREP_ERRORLINE1} " != " " ]; then grep "${GREP_ERRORLINE1}" ${TESTLOG}; fi;
            if [ "${GREP_ERRORLINE2} " != " " ]; then grep "${GREP_ERRORLINE2}" ${TESTLOG}; fi;
            echo ""
          fi
        fi
      done

    done

    hdiutil detach ${FS_MNT}
  fi
fi

##
## Unit test report summary
##
if [ ${CANCEL} == ${OPT_OFF} ] ; then 

  echo "--------------------------------------------------------------------------------"
  ENDTIME=`date`
  OSVERSION=`sw_vers -buildVersion`
  if [ "${LOG_STDOUT}" == "${OPT_ON}" ]; then 
    SUMMARYLOG="/dev/stdout"
  else
    SUMMARYLOG="${QUALLOGDIR}/summary.txt"
  fi
  echo "Unit test qualification completed" > ${SUMMARYLOG}
  echo "Test harness ${HARNESS} based on sqlite version ${SQLITE_VERSION}" >> ${SUMMARYLOG}
  echo "Qualification performed on system version ${OSVERSION}" >> ${SUMMARYLOG}
  echo "  Started   ${STARTTIME}" >> ${SUMMARYLOG}
  echo "  Completed ${ENDTIME}" >> ${SUMMARYLOG}
  echo "" >> ${SUMMARYLOG}

  if [ "${LOG_STDOUT}" == "${OPT_OFF}" ]; then 
    ALLTESTLOGS=`(cd ${QUALLOGDIR}; ls test_*.log)`
    for TESTLOG in ${ALLTESTLOGS}; do
      LOGNAME=`echo ${TESTLOG} | sed 's/test_\(.*\)\.log/\1/'`
      if [ ${HARNESS} == "TH3" ]; then
        FAILURES=`grep 'FAILED' ${QUALLOGDIR}/${TESTLOG}`
      else
        FAILURES=`grep 'Failures on these tests' ${QUALLOGDIR}/${TESTLOG} | sed 's/Failures on these tests://'`
      fi
      ERRORS=`grep 'errors out of' ${QUALLOGDIR}/${TESTLOG}`
      echo "${LOGNAME}: ${ERRORS}" >> ${SUMMARYLOG}
      if [ "${FAILURES} " != " " ]; then 
        echo "  ${FAILURES}" >> ${SUMMARYLOG}
      fi
      grep -B 2 'Abort trap' ${QUALLOGDIR}/${TESTLOG} >> ${SUMMARYLOG}
      echo "" >> ${SUMMARYLOG}
    done
    cat ${QUALLOGDIR}/summary.txt
  fi

fi
