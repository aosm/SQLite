#
# rebuild_derived.sh 
#

#
# Creating derived source 
#
BDIR=/tmp/rebuild_derived
BLOG="${BDIR}/build.log"
SDIR="`pwd`"
DDIR=${SDIR}/derived_source
DFILES="config.h keywordhash.h sqlite3.h parse.h parse.c opcodes.h opcodes.c sqlite3.c"

echo "Rebuilding derived files in ${BDIR} (build log in ${BLOG})"
rm -rf ${BDIR}
mkdir ${BDIR}
( cd ${BDIR}; python ${SDIR}/scripts/configure.py >& ${BLOG} )
( cd ${BDIR}; make ${DFILES} >> ${BLOG} )

CHANGEDFILES=""
REPLACEDFILES=""

for DFILE in ${DFILES}; do
  DFILEDIFF=`diff -N -q --ignore-matching-lines="This amalgamation was generated on" ${BDIR}/${DFILE} ${DDIR}/${DFILE}`
  if [ "${DFILEDIFF} " != " " ]; then
    CHANGEDFILES="${CHANGEDFILES} ${DFILE}"
  fi
done

if [ "${CHANGEDFILES} " == " " ]; then
  echo "No derived files were changed, nothing to do"
else
  echo "Changed derived files: ${CHANGEDFILES}"

  for DFILE in ${CHANGEDFILES}; do
    
    echo -n "Replace ${DFILE}? (y/n/d [n]) "
    read
    if [ "${REPLY} " == "d " ]; then
      RAWDIFF=`echo "diff ${BDIR}/${DFILE} ${DDIR}/${DFILE}"; diff -N --ignore-matching-lines="This amalgamation was generated on" ${BDIR}/${DFILE} ${DDIR}/${DFILE}`
      echo "${RAWDIFF}" | less
      echo -n "Replace ${DFILE}? (y/n [n]) "
      read
    fi
    if [ "${REPLY} " == "y " ]; then
      cp ${BDIR}/${DFILE} ${DDIR}/${DFILE}
      REPLACEDFILES="${REPLACEDFILES} ${DFILE}"
    fi
  done

  if [ "${REPLACEDFILES} " == " " ]; then
    echo "No derived files were replaced"
  else
    echo "Replaced files: ${REPLACEDFILES}"
  fi

fi

