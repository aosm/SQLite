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

#pragma mark #includes

#include <sys/param.h>
#include <sys/mount.h>

#include "NSSQLiteLockingSupport.h"

#include <sys/file.h>
#include <sys/attr.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syslimits.h>
#include <sys/stat.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/attr.h>

#pragma mark AFP Support

struct ByteRangeLockPB2
{
    unsigned long long offset;			/* offset to first byte to lock */
    unsigned long long length;			/* nbr of bytes to lock */
    unsigned long long retRangeStart;		/* nbr of first byte locked if successful */
    unsigned char unLockFlag;			/* 1 = unlock, 0 = lock */
    unsigned char startEndFlag;			/* 1 = rel to end of fork, 0 = rel to start */
    int fd;					/* file descriptor to assoc this lock with */
};

#define afpfsByteRangeLock2FSCTL	_IOWR('z', 23, struct ByteRangeLockPB2)

typedef struct AFPLockingContext {
    unsigned long long sharedLockByte;
    int currentLockType;
} AFPLockingContext;

// return 0 on success
// return 1 on failure
static int _AFPFSSetLock(const char *path, int fd, unsigned long long offset, unsigned long long length, int setLockFlag)
{
    struct ByteRangeLockPB2	pb;
    int                     err;
    
    pb.unLockFlag = setLockFlag ? 0 : 1;
    pb.startEndFlag = 0;
    pb.offset = offset;
    pb.length = length; 
    pb.fd = fd;
    err = fsctl(path, afpfsByteRangeLock2FSCTL, &pb, 0);
    if ( err==-1 ) {
        //!        fprintf(stdout, "Failed to fsctl() '%s' %d %s\n", id->filePath, errno, strerror(errno));
        return 1; // error
    } else {
        return 0;
    }
}

#define PENDING_BYTE      0x40000000  /* First byte past the 1GB boundary */
#define RESERVED_BYTE     (PENDING_BYTE+1)
#define SHARED_FIRST      (PENDING_BYTE+2)
#define SHARED_SIZE       510

// return 0 if no lock held
// return 1 if lock is held
static int _afpCheckReservedLock(const char *path, int pathFD, SQLite3OsLockingCallbacks *lockingState) {
    AFPLockingContext *lockingContext = (AFPLockingContext *) lockingState->context;
    
    if (lockingContext->currentLockType == RESERVED_LOCK) {
        return 1; // already have a reserved lock
    } else {
        int rc = _AFPFSSetLock(path, pathFD, RESERVED_BYTE, 1, 1);  // lock the byte
        if (!rc)
            _AFPFSSetLock(path, pathFD, RESERVED_BYTE, 1, 0); // if it succeeded, unlock it
        return rc; // if we could get the lock, we don't have the lock.  Got it?
    }
}

static int _afpLock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState)
{
    AFPLockingContext *lockingContext = (AFPLockingContext *) lockingState->context;
    int rc = SQLITE_OK;
    int lockAttemptFailure = 0;
    int newLocktype;
    int gotPendingLock = 0;
    
    if( lockingContext->currentLockType >= locktype ){
        return SQLITE_OK;
    }
    
    /* Make sure the locking sequence is correct
        */
    assert( lockingContext->currentLockType!=NO_LOCK || locktype==SHARED_LOCK );
    assert( locktype!=PENDING_LOCK );
    assert( locktype!=RESERVED_LOCK || lockingContext->currentLockType==SHARED_LOCK );
    
    /* Lock the PENDING_LOCK byte if we need to acquire a PENDING lock or
        ** a SHARED lock.  If we are acquiring a SHARED lock, the acquisition of
        ** the PENDING_LOCK byte is temporary.
        */
    newLocktype = lockingContext->currentLockType;
    if( lockingContext->currentLockType==NO_LOCK
        || (locktype==EXCLUSIVE_LOCK && lockingContext->currentLockType==RESERVED_LOCK) ){
        lockAttemptFailure = _AFPFSSetLock(path, pathFD, PENDING_BYTE, 1, 1);
        gotPendingLock = !lockAttemptFailure;
    }
    
    /* Acquire a shared lock
        */
    if( locktype==SHARED_LOCK && (!lockAttemptFailure) ){
        assert( lockingContext->currentLockType==NO_LOCK );
        int lk = rand(); // note that the quality of the randomness doesn't matter that much
        lockingContext->sharedLockByte = (lk & 0x7fffffff)%(SHARED_SIZE - 1);
        lockAttemptFailure = _AFPFSSetLock(path, pathFD, SHARED_FIRST+lockingContext->sharedLockByte, 1, 1);
        if( !lockAttemptFailure ){
            newLocktype = SHARED_LOCK;
        }
    }
    
    /* Acquire a RESERVED lock
        */
    if( locktype==RESERVED_LOCK && (!lockAttemptFailure) ){
        assert( lockingContext->currentLockType==SHARED_LOCK );
        lockAttemptFailure = _AFPFSSetLock(path, pathFD, RESERVED_BYTE, 1, 1);
        if( !lockAttemptFailure ){
            newLocktype = RESERVED_LOCK;
        }
    }
    
    /* Acquire a PENDING lock
        */
    if( locktype==EXCLUSIVE_LOCK && (!lockAttemptFailure) ){
        newLocktype = PENDING_LOCK;
        gotPendingLock = 0;
    }
    
    /* Acquire an EXCLUSIVE lock
        */
    if( locktype==EXCLUSIVE_LOCK && (!lockAttemptFailure) ){
        assert( lockingContext->currentLockType>=SHARED_LOCK );
        lockAttemptFailure = _AFPFSSetLock(path, pathFD, SHARED_FIRST + lockingContext->sharedLockByte, 1, 0);
        lockAttemptFailure = _AFPFSSetLock(path, pathFD, SHARED_FIRST, SHARED_SIZE, 1);
        if( (!lockAttemptFailure) ){
            newLocktype = EXCLUSIVE_LOCK;
        }
    }
    
    /* If we are holding a PENDING lock that ought to be released, then
        ** release it now.
        */
    if( gotPendingLock && locktype==SHARED_LOCK ){
        _AFPFSSetLock(path, pathFD, PENDING_BYTE, 1, 0);
    }
    
    /* Update the state of the lock has held in the file descriptor then
        ** return the appropriate result code.
        */
    if( (!lockAttemptFailure) ){
        rc = SQLITE_OK;
    }else{
        rc = SQLITE_BUSY;
    }
    lockingContext->currentLockType = newLocktype;
    return rc;
}

static int _afpUnlock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState) {
    AFPLockingContext *lockingContext = (AFPLockingContext *) lockingState->context;
    int type;
    assert( locktype<=SHARED_LOCK );
    type = lockingContext->currentLockType;
    if( type>=EXCLUSIVE_LOCK ){
        _AFPFSSetLock(path, pathFD, SHARED_FIRST, SHARED_SIZE, 0);
    }
    if( type>=RESERVED_LOCK ){
        _AFPFSSetLock(path, pathFD, RESERVED_BYTE, 1, 0);
    }
    if( locktype==NO_LOCK && type>=SHARED_LOCK && type<EXCLUSIVE_LOCK ){
        _AFPFSSetLock(path, pathFD, SHARED_FIRST + lockingContext->sharedLockByte, 1, 0);
    }
    if( type>=PENDING_LOCK ){
        _AFPFSSetLock(path, pathFD, PENDING_BYTE, 1, 0);
    }
    lockingContext->currentLockType = locktype;
    return SQLITE_OK;
}

static void _freeAFPCallbackStruct(void *callbackStruct) {
    SQLite3OsLockingCallbacks *_callbackStruct = (SQLite3OsLockingCallbacks *) callbackStruct;
    free((void *)(_callbackStruct->cachedPath));
    free(_callbackStruct->context);
    free(_callbackStruct);
}

#pragma mark flock() style locking
typedef struct FlockLockingContext {
    int currentLockType;
} FlockLockingContext;

static void _freeFlockCallbackStruct(void *callbackStruct) {
    SQLite3OsLockingCallbacks *_callbackStruct = (SQLite3OsLockingCallbacks *) callbackStruct;
    free((void *)(_callbackStruct->cachedPath));
    free(_callbackStruct->context);
    free(_callbackStruct);
}

static int _flockCheckReservedLock(const char *path, int pathFD, SQLite3OsLockingCallbacks *lockingState) {
    FlockLockingContext *lockingContext = (FlockLockingContext *) lockingState->context;
    
    if (lockingContext->currentLockType == RESERVED_LOCK) {
        return 1; // already have a reserved lock
    } else {
        // attempt to get the lock
        int rc = flock(pathFD, LOCK_EX | LOCK_NB);
        if (!rc) {
            // got the lock, unlock it
            flock(pathFD, LOCK_UN);
            return 0;  // no one has it reserved
        }
        return 1; // someone else might have it reserved
    }
}

static int _flockLock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState) {
    FlockLockingContext *lockingContext = (FlockLockingContext *) lockingState->context;

    // if we already have a lock, it is exclusive.  Just adjust level and punt on outta here.
    if (lockingContext->currentLockType > NO_LOCK) {
        lockingContext->currentLockType = locktype;
        return SQLITE_OK;
    }

    // grab an exclusive lock
    int rc = flock(pathFD, LOCK_EX | LOCK_NB);
    if (rc) {
        // didn't get, must be busy
        return SQLITE_BUSY;
    } else {
        // got it, set the type and return ok
        lockingContext->currentLockType = locktype;
        return SQLITE_OK;
    }
}

static int _flockUnlock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState) {
    FlockLockingContext *lockingContext = (FlockLockingContext *) lockingState->context;

    assert( locktype<=SHARED_LOCK );

    // no-op if possible
    if( lockingContext->currentLockType==locktype ){
        return SQLITE_OK;
    }
    
    // shared can just be set because we always have an exclusive
    if (locktype==SHARED_LOCK) {
        lockingContext->currentLockType = locktype;
        return SQLITE_OK;
    }
    
    // no, really, unlock.
    int rc = flock(pathFD, LOCK_UN);
    if (rc)
        return SQLITE_IOERR;
    else {
        lockingContext->currentLockType = NO_LOCK;
        return SQLITE_OK;
    }
}

#pragma mark Old-School .lock file based locking
typedef struct _LegacyLockingStateContext {
    char *lockpath;
    int debug;
    int currentLockType;
} LegacyLockingStateContext;

static void _freeLegacyLockingCallbackStructure(void *callbackStruct) {
    SQLite3OsLockingCallbacks *_callbackStruct = (SQLite3OsLockingCallbacks *) callbackStruct;
    LegacyLockingStateContext *_context = _callbackStruct->context;
    free((void *)(_callbackStruct->cachedPath));
    free(_context->lockpath);
    free(_context);
    free(_callbackStruct);
}

static int _legacyCheckReservedLock(const char *path, int pathFD, SQLite3OsLockingCallbacks *lockingState) {
    LegacyLockingStateContext *lockingContext = (LegacyLockingStateContext *) lockingState->context;
    
    if (lockingContext->currentLockType == RESERVED_LOCK) {
        return 1; // already have a reserved lock
    } else {
        struct stat statBuf;
        if (lstat(lockingContext->lockpath,&statBuf) == 0)
            // file exists, someone else has the lock
            return 1;
        else
            // file does not exist, we could have it if we want it
            return 0;
    }
}

static int _legacyLock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState) {
    LegacyLockingStateContext *lockingContext = (LegacyLockingStateContext *) lockingState->context;
    
    // if we already have a lock, it is exclusive.  Just adjust level and punt on outta here.
    if (lockingContext->currentLockType > NO_LOCK) {
        lockingContext->currentLockType = locktype;
        
        /* Always update the timestamp on the old file */
        utimes(lockingContext->lockpath,NULL);
        return SQLITE_OK;
    }
    
    // check to see if lock file already exists
    struct stat statBuf;
    if (lstat(lockingContext->lockpath,&statBuf) == 0){
        return SQLITE_BUSY; // it does, busy
    }
    
    // grab an exclusive lock
    int fd = open(lockingContext->lockpath,O_RDONLY|O_CREAT|O_EXCL,0600);
    if (fd < 0)
        return SQLITE_BUSY; // failed to open/create the file, someone else may have stolen the lock
    close(fd);

    // got it, set the type and return ok
    lockingContext->currentLockType = locktype;
    return SQLITE_OK;
}

static int _legacyUnlock(const char *path, int pathFD, int locktype, SQLite3OsLockingCallbacks *lockingState) {
    LegacyLockingStateContext *lockingContext = (LegacyLockingStateContext *) lockingState->context;
    
    assert( locktype<=SHARED_LOCK );
    
    // no-op if possible
    if( lockingContext->currentLockType==locktype ){
        return SQLITE_OK;
    }
    
    // shared can just be set because we always have an exclusive
    if (locktype==SHARED_LOCK) {
        lockingContext->currentLockType = locktype;
        return SQLITE_OK;
    }
    
    // no, really, unlock.
    unlink(lockingContext->lockpath);
    lockingContext->currentLockType = NO_LOCK;
    return SQLITE_OK;
}

#pragma mark Callback structure instance constructors

SQLite3OsLockingCallbacks *_NSNewAFPSQLiteLockingCallbacks(const char *path, int fd) {
    SQLite3OsLockingCallbacks *_afpLockingCallbacks;
    
    _afpLockingCallbacks = calloc(1, sizeof(SQLite3OsLockingCallbacks));

    _afpLockingCallbacks->checkReservedFuncPtr = (sqlite3OsCheckReservedLockPathCallback)_afpCheckReservedLock;
    _afpLockingCallbacks->lockFuncPtr = (sqlite3OsLockPathCallback)_afpLock;
    _afpLockingCallbacks->unlockFuncPtr = (sqlite3OsUnlockPathCallback)_afpUnlock;

    _afpLockingCallbacks->cachedPath = strdup(path);
    _afpLockingCallbacks->cachedFD = fd;
    _afpLockingCallbacks->context = calloc(1, sizeof(AFPLockingContext));
    _afpLockingCallbacks->free = _freeAFPCallbackStruct;
    
    return _afpLockingCallbacks;
}

SQLite3OsLockingCallbacks *_NSNewLegacySQLiteLockingCallbacks(const char *path, int fd) {
    SQLite3OsLockingCallbacks *_legacyLockingCallbacks;

    _legacyLockingCallbacks = calloc(1, sizeof(SQLite3OsLockingCallbacks));
        
    _legacyLockingCallbacks->checkReservedFuncPtr = (sqlite3OsCheckReservedLockPathCallback)_legacyCheckReservedLock;
    _legacyLockingCallbacks->lockFuncPtr = (sqlite3OsLockPathCallback)_legacyLock;
    _legacyLockingCallbacks->unlockFuncPtr = (sqlite3OsUnlockPathCallback)_legacyUnlock;
    
    _legacyLockingCallbacks->cachedPath = strdup(path);
    _legacyLockingCallbacks->cachedFD = fd;
    _legacyLockingCallbacks->context = calloc(1, sizeof(LegacyLockingStateContext));
    _legacyLockingCallbacks->free = _freeLegacyLockingCallbackStructure;

    LegacyLockingStateContext *lockingContext = (LegacyLockingStateContext *) _legacyLockingCallbacks->context;

    lockingContext->debug = 0;        
    
    lockingContext->lockpath = malloc(strlen(path) + strlen(".lock") + 1);
    sprintf(lockingContext->lockpath,"%s.lock",path);
    
    return _legacyLockingCallbacks;
}

SQLite3OsLockingCallbacks *_NSNewFlockLockingCallbacks(const char *path, int fd) {
    SQLite3OsLockingCallbacks *_flockLockingCallbacks;
    
    _flockLockingCallbacks = calloc(1, sizeof(SQLite3OsLockingCallbacks));
    
    _flockLockingCallbacks->checkReservedFuncPtr = (sqlite3OsCheckReservedLockPathCallback)_flockCheckReservedLock;
    _flockLockingCallbacks->lockFuncPtr = (sqlite3OsLockPathCallback)_flockLock;
    _flockLockingCallbacks->unlockFuncPtr = (sqlite3OsUnlockPathCallback)_flockUnlock;
    
    _flockLockingCallbacks->cachedPath = strdup(path);
    _flockLockingCallbacks->cachedFD = fd;
    _flockLockingCallbacks->context = calloc(1, sizeof(FlockLockingContext));
    _flockLockingCallbacks->free = _freeFlockCallbackStruct;
    
    return _flockLockingCallbacks;
}

const static SQLite3OsLockingCallbacks  _passThroughToSQliteCallbacks __attribute__ ((nocommon)) = { NULL, NULL, NULL, NULL, 0, NULL, NULL };

SQLite3OsLockingCallbacks *_NSNewUseInternalSQLiteLockingCallbacks(const char *path, int fd) {
    // plug in lock stealer fnction here as context(?)
    return (SQLite3OsLockingCallbacks *) &_passThroughToSQliteCallbacks;
}

const static SQLite3OsLockingCallbacks  _noOpLockingCallbacks __attribute__ ((nocommon)) = { NULL, NULL, NULL, NULL, 0, NULL, NULL };

SQLite3OsLockingCallbacks *_NSNewNoOpSQLiteLockingCallbacks(const char *path, int fd) {
    //! might need to change this to have actual no-op functions instead of falling through
    return (SQLite3OsLockingCallbacks *) &_noOpLockingCallbacks;
}

#pragma mark File system detection & selection routines

static _NSFSType _fsAtPathShouldActLike(const char *path, int fd) {
    // test fcntl
    struct flock lockInfo;
    
    lockInfo.l_len = 1;
    lockInfo.l_start = 0;
    lockInfo.l_whence = SEEK_SET;
    lockInfo.l_type = F_RDLCK;
    
    if (fcntl(fd, F_GETLK, (int) &lockInfo) != -1) {
        return _NSAdvisoryLocksWorkFS;
    } 
    
    // test flock
    /*
      OK -- this proves to be a lose.  Specifically, NFS on 10.2.8 will return "yeah, sure" for this, even though flock() calls that try to set an actual lock will fail miserably with ENOTSUP.   While we could try to set and remove a lock, we will be screwed if something else has already set the lock.   And, really, unlocking isn't a very good test anyway because someone else may have locked the file and the unlock will fail for a perfectly valid reason.  So, flock() is out entirely.
    if (flock(fd, LOCK_UN | LOCK_NB) != -1) {
      fprintf(stdout, "Apparently, flock works on %s\n", path);
        return _NSFlockLocksWorkFS;
	}*/
    
    // fall back to legacy
    return _NSDotLockFilesFS;
}

_NSFSType _NSFSTypeAtPath(const char *path, int fd) {
    struct statfs fsInfo;
    
    if (statfs(path, &fsInfo) == -1)
        return _fsAtPathShouldActLike(path, fd);
    
    if (fsInfo.f_flags & MNT_RDONLY)
        return _NSReadOnlyFS;
    
    if(!strcmp(fsInfo.f_fstypename, "hfs"))
        return _NSHFS;
    if(!strcmp(fsInfo.f_fstypename, "afpfs"))
        return _NSAFPFS;
    if(!strcmp(fsInfo.f_fstypename, "nfs")) {
        return _fsAtPathShouldActLike(path, fd);
    }
    if(!strcmp(fsInfo.f_fstypename, "ufs"))
        return _NSUFS;
    if(!strcmp(fsInfo.f_fstypename, "smbfs"))
        return _NSSMBFS;
    if(!strcmp(fsInfo.f_fstypename, "msdos"))
        return _NSMSDOSFS;
    if(!strcmp(fsInfo.f_fstypename, "webdav"))
        return _NSWebDavFS;
    
    return _fsAtPathShouldActLike(path, fd);
}

SQLite3OsLockingCallbacks *_NSSQLiteLockingCallbacksForFile(const char *path, int fd) {
    if (!path)
        return _NSNewUseInternalSQLiteLockingCallbacks(path, fd);

    switch (_NSFSTypeAtPath(path, fd)) {
        case _NSHFS:
        case _NSUFS:
        case _NSNFSFS:
        case _NSAdvisoryLocksWorkFS:
            return _NSNewUseInternalSQLiteLockingCallbacks(path, fd);
            break;
        case _NSAFPFS:
            return _NSNewAFPSQLiteLockingCallbacks(path, fd);
            break;
        case _NSMSDOSFS:  // need NTFS, too?
        case _NSLegacyNFSFS:
        case _NSDotLockFilesFS:
            return _NSNewLegacySQLiteLockingCallbacks(path, fd);
        case _NSSMBFS:
        case _NSFlockLocksWorkFS:
            return _NSNewFlockLockingCallbacks(path, fd);
            break;
        case _NSReadOnlyFS: // generic for indicating that the filesystem is read-only, so locking is moot */
            return _NSNewNoOpSQLiteLockingCallbacks(path, fd);
        case _NSWebDavFS:
        case _NSUnknownFS:  // *punt* WebDAV/iDisk might fall into this.  Might.  This is a very hard issue.
            return NULL;
    }
    return NULL;
}
