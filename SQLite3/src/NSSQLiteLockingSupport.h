/*
 * All copyright in this software has been dedicated to the public domain.
 *
 * This software is provided on an "AS IS" basis. THE AUTHORS AND PROVIDERS OF
 * THIS SOFTWARE MAKE NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE, REGARDING THIS SOFTWARE OR ITS USE AND
 * OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 *
 * IN NO EVENT SHALL THE AUTHORS OR PROVIDERS OF THIS SOFTWARE BE LIABLE FOR
 * ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE
 * USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THIS SOFTWARE,
 * HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING
 * NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHORS AND/OR
 * PROVIDERS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Typical usage from <rdar://problem/3635043>
  #import <NSSQLiteLockingSupport.h>
   ...
  sqlite3OsSetLockingCallback(_NSSQLiteLockingCallbacksForFile);

  Note that SQLite uses a mutex such that only one thread can be calling into these functions at a time.
*/

/* this will enable locking callbacks when sqlite3.h is imported */
#define ENABLE_LOCKING_CALLBACKS 1
#include <sqlite3.h>

typedef enum {
	_NSHFS,
	_NSUFS,
	_NSMSDOSFS,  // need NTFS, too?
	_NSAFPFS,
	_NSNFSFS,
	_NSLegacyNFSFS,
	_NSSMBFS,
	_NSReadOnlyFS, // generic for indicating that the filesystem is read-only, so locking is moot
    _NSWebDavFS,
	_NSUnknownFS,  // *punt* WebDAV/iDisk might fall into this.  Might.  This is a very hard issue.
    _NSAdvisoryLocksWorkFS,
    _NSFlockLocksWorkFS,
    _NSDotLockFilesFS
} _NSFSType;

_NSFSType _NSFSTypeAtPath(const char *path, int fd);

SQLite3OsLockingCallbacks *_NSNewUseInternalSQLiteLockingCallbacks(const char *path, int fd);
SQLite3OsLockingCallbacks *_NSNewAFPSQLiteLockingCallbacks(const char *path, int fd);
SQLite3OsLockingCallbacks *_NSNewLegacySQLiteLockingCallbacks(const char *path, int fd);
SQLite3OsLockingCallbacks *_NSNewNoOpSQLiteLockingCallbacks(const char *path, int fd);
SQLite3OsLockingCallbacks *_NSNewFlockLockingCallbacks(const char *path, int fd);

SQLite3OsLockingCallbacks *_NSSQLiteLockingCallbacksForFile(const char *path, int fd);
