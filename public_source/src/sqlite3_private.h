/*
 *  sqlite3_private.h
 */

#ifndef _SQLITE3_PRIVATE_H
#define _SQLITE3_PRIVATE_H

#define SQLITE_LOCKSTATE_OFF    0
#define SQLITE_LOCKSTATE_ON     1
#define SQLITE_LOCKSTATE_NOTADB 2
#define SQLITE_LOCKSTATE_ERROR  -1

#define SQLITE_LOCKSTATE_ANYPID -1

/* 
** Test a file path for sqlite locks held by a process ID (-1 = any PID). 
** Returns one of the following integer codes:
** 
**   SQLITE_LOCKSTATE_OFF    no active sqlite file locks match the specified pid
**   SQLITE_LOCKSTATE_ON     active sqlite file locks match the specified pid
**   SQLITE_LOCKSTATE_NOTADB path points to a file that is not an sqlite db file
**   SQLITE_LOCKSTATE_ERROR  path was not vaild or was unreadable
**
** There is no support for identifying db files encrypted via SEE encryption
** currently.  Zero byte files are tested for sqlite locks, but if no sqlite 
** locks are present then SQLITE_LOCKSTATE_NOTADB is returned.
**
** BUGS: when passing a real process ID if a shared lock is held by two
** processes and the other process ID is returned by fcntl then
** _sqlite3_lockstate will return SQLITE_LOCKSTATE_OFF (being tracked by
** <rdar://problem/7539626> Add PID-specific F_GETLKPID command to fcntl)
*/
extern int _sqlite3_lockstate(const char *path, pid_t pid);

/*
** Purges eligible purgable memory regions (holding only unpinned pages) from 
** the page cache
*/
extern void _sqlite3_purgeEligiblePagerCacheMemory(void);

/*
** Pass the SQLITE_TRUNCATE_DATABASE operation code to sqlite3_file_control() 
** to truncate a database and its associated journal file to zero length.
*/
#define SQLITE_TRUNCATE_DATABASE      101

/*
** Pass the SQLITE_REPLACE_DATABASE operation code to sqlite3_file_control() 
** and a sqlite3 pointer to another open database file to safely copy the 
** contents of that database file into the receiving database.
*/
#define SQLITE_REPLACE_DATABASE       102

#endif
