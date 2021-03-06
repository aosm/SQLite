SQLite Project Overview
-----------------------
SQLite is maintained by sqlite.org, this project workarea specifies the configuration settings for building sqlite for macosx deployment.  This project is based on the public source releases of sqlite which are customized as minimally as possible and live in the public_source subdirectory.  Unlike the public source releases, this project uses Xcode as the build system in order to simplify a variety of platform deployment and debugging needs.  

SQLite Project Resources
------------------------
public_source/            - sqlite sources from sqlite.org, including internal customizations

derived_source/           - files that are generated via configure & the generated Makefile (see scripts/rebuild_derived.sh)

Notes/
  README.txt              - this file
  EngineeringNotes.txt    - useful build/config details
  TestingNotes.txt        - known unit test failures
  ProxyLocking.txt        - describes how proxy locking works and how to use it
  
scripts/
  build_th3.sh            - script to create th3_fulltest executable against current system SDK
  qualify_th3.sh          - bash script to run th3 tests against all supported architectures and file systems
  buildroot.sh            - script to create SQLite root (use -tcl to build an SQLite_tcl root, or -embedded to build a device root)
  configure.py            - script to invoke configure to produce a Makefile that will build sqlite in the same way the Xcode project does (used by qualify.sh).
  qualify.sh              - bash script to run unit tests against all supported architectures and file systems
  rebuild_derived.sh      - rebuilds and REPLACES the contents of the derived_sources directory
  installdyliblinks.sh    - install script used by the 'sqlite production dylib' target
  installtcldyliblinks.sh - install script used by the 'sqlite tcl module' target

TH3/
  README_th3.txt          - information about where to get TH3 sources and how to access them
  th3_public_source/      - TH3 public sources
  th3_fulltest.th3script  - Xcode th3script file to construct fulltest
  th3_quicktest.th3script - Xcode th3script file to construct quicktest
  th3_onetest.th3script   - Xcode th3script file to construct onetest

packaging/
  SQLite.plist            - Open source project version info
  SQLite.txt              - Open source project license info
  dpkg                    - debian package information (not sure if this is used)

VisualStudio/
  SQLite.sln             - Visual studio solution file
  SQLite/                - Visual studio project resources
    SQLite.vcproj        - Visual C++ project file
    includes/
      autoversion.h      - Defines version numbers for SQLite3.DLL

./
  SQLite.xcodeproj        - Xcode project for building, installing & submitting
  sqlite-3.x.y.tar.gz (or
  SQLite-xxxxxxxxxx.zip)  - public sources used as basis for public_source


Submission Requirements
-----------------------
1. Ensure the derived sources are up to date
2. Run Pre-submission Qualification tests
3. Review/update the TestingNotes.txt
4. Make sure the following files are up to date with version & customization details: 
	SQLite.plist (openssl dgst -sha1 <public-source-tarball>)
    dpkg/control
	CustomizationNotes.txt
	VisualStudio/SQLite/includes/autoversion.h
5. Build and post installable root to ~xdesign/roots/sqlite


Updating from new public release
--------------------------------
1. Reconcile the differences between the old Makefile and the new Makefile - note that this means comparing our current Makefile.in against the Makefile.in from the public release it was based on, it may be necesssary to preserve those diffs when integrating changes in the new release's makefile
2. Add/remove source files in the appropriate targets (eg. sqlite3 testing dylib)
3. Rebuild the derived sources
4. Other changes to the Makefile/configure script may require changes at the project level (eg. OTHER_CFLAGS)


Building Production Roots
-------------------------
1. Change directories to the top level and run buildroot.sh (. scripts/buildroot.sh)
2. Roots are installed into /tmp/Sqlite.roots/Sqlite~dst
3. Root tar ball is created and appropriately named in /tmp as SQLite....
WINDOWS
1. See the instructions on setting up buildit here: http://tao.apple.com/cgi-bin/wiki?Buildit_For_Windows
2. Quit visual studio and open a command window (shift-right click on the desktop)
2. Enter the following command:
	perl w:\bin/builditwin -altPath=VisualStudio -buildTool=Solution c:\cygwin\home\Adam\Work\SQLite\trunk -- "file = SQLite.sln"

Building & using debug + os trace
---------------------------------
1. Set "sqlite3OSTrace = 1;" in public_source/src/os_common.h
2. Build a debug sqlite shared library:
   mkdir /tmp/sqlitedebug
   cd /tmp/sqlitedebug
   .../configure.py -debug
   make
3. Use the debug library:
   export DYLD_LIBRARY_PATH=/tmp/sqlitedebug/.libs/

(Note, to test a 64-bit or other non-default architecture variant, pass the following to configure.py: -cflags="-arch x86_64 -arch ppc" )

3. See the TestingNotes.txt document for known unit test failures.


Pre-submission Qualification
----------------------------
1. Run unit tests on all supported architectures [i386, x86_64, ppc] and file system configurations [HFS, AFP, NFS, SMB, MSDOS] -- this can now be done automatically via the qualify.sh script.
2. Run CoreData unit tests.
3. Run must-not-break apps that are clients of sqlite: Mail, Safari, iChat, Address Book, iCal.  Confirm apps run normally, no unexpected errors/logging in Console.
4. tclsh
  % package require sqlite3
  % load /System/Library/Tcl/sqlite3/libtclsqlite3.dylib
(previous path was)
  % load /usr/lib/sqlite3/libtclsqlite3.dylib


Testing on MS-DOS (done automatically via the qualify.sh script.)
-----------------
1. Create an MS-DOS disk image:
	hdiutil create -megabytes 100 -fs MS-DOS dostest -volname DOS
2. Mount it
	hdiutil attach dostest.db
3. Run the unit tests within that file system
	cd /Volumes/DOS; .../configure.py ; make ; make test;


Testing on embedded platforms
-----------------------------
INSTRUCTIONS FOR TH3
1. Prepare device for command line executable execution
	nvram boot-args="debug=0x144 amfi_allow_any_signature=1 amfi_unrestrict_task_for_pid=1 cs_enforcement_disable=1"
	sysctl -w security.mac.sandbox.debug_mode=1 (allows running from Xcode)
2. Build th3_fulltest
3. Sync over th3_fulltest binary if not synced via Xcode: rsync -av <Xcode build dir>/th3_fulltest rsync://root@localhost:10873/root/
4. Connect to device and run in a test directory ... and wait about 6 hours

INSTRUCTION FOR LEGACY TESTFIXTURE
1. Grab the Tcl root from ~xdesign/roots/sqlite/
2. Install the Tcl root in the SDKROOT and in / on the device
3. Build the testfixture target for the SDK/device
4. Copy over the testfixture and the public_source directory
5. Run testfixture on the device: /tmp/testfixture /tmp/public_source/test/veryquick.test


Testing on Windows platforms
----------------------------
1. Set up a cygwin environment according to the WebKit instructions found here: http://webkit.org/building/build.html
2. Create an empty directory for building and testing
3. From that directory, issue the following commands:
  % .../scripts/configure.py -windows
  % make test
