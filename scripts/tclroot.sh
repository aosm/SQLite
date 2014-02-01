
SDKROOT="/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS3.0.Internal.sdk"
TDIR="/tmp/tcl_root_dir"
TCLTARBALL="/Network/Servers/harris/Volumes/haus/xdesign/roots/sqlite/tcl-75_7A161.tgz"

if [ ! -d ${SDKROOT} ] ; then 
  echo "Can't install TCL root, SDK is missing"
else
  rm -rf ${TDIR}
  mkdir ${TDIR}

  if [ ! -d "${SDKROOT}/Library/Frameworks/Tcl.framework" ]; then
    echo "Installing TCL root in ${SDKROOT}"
    (cd ${SDKROOT}; sudo tar xfz ${TCLTARBALL})
  fi

  echo "Syncing TCL root"
  (cd ${TDIR}; tar xfz ${TCLTARBALL})
  rsync -av ${TDIR}/ rsync://root@localhost:10873/root/
  
  echo "To copy testfixture:"
  echo "  rsync -av /Volumes/Data/Build/Debug-iphoneos/testfixture rsync://root@localhost:10873/root/var/root/"
  echo ""
  echo "To copy test harness resources:"
  echo "  (mkdir /tmp/test; cd /tmp/test; svn export -q svn+ssh://src.apple.com/svn/devtech/Persistence/SQLite/trunk/public_source)"
  echo "  rsync -av /tmp/test/public_source rsync://root@localhost:10873/root/var/root/"
fi;

