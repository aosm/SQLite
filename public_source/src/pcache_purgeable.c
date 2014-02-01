/*
 *  pcache_purgeable.c
 *
 *  Created by Adam on 1/21/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include "sqliteInt.h"

#if !defined(SQLITE_OMIT_PURGEABLE_PCACHE)

#include <malloc/malloc.h>
#include <dispatch/dispatch.h>
#include <mach/vm_map.h>
#include <mach/vm_statistics.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <libkern/OSAtomic.h>

//#define TRACE_PURGEABLE_PCACHE  0

/* 
** OMIT_VM_PURGABLE
** Defining OMIT_VM_PURGABLE disables all vm_purgable_control calls, preventing
** the vm subsystem from purging memory chunks when the system is under memory
** pressure.
*/


/* 
** Page data is retrieved from a purgeable memory chunk.  The page 'header' 
** includes a pointer to that page data block and the page data block includes 
** a prefix (sizeof(void*)) to point back to the page header.  When all of the 
** pages in a chunk are unpinned, then the chunk of memory can be marked 
** VM_PURGABLE_VOLATILE (the PURGEABLE_CHUNK_QUEUE_SIZE determines how many
** chunks are queued up for marking as volatile before the vm_purgable_control
** call is actually issued).
** 
** When individual page cache pages are marked as pinned they are removed from 
** the pcache LRU list.
*/

/* A unique vm memory tag for sqlite page cache allocated regions */
#ifndef VM_MEMORY_SQLITE
# define VM_MEMORY_SQLITE                     62
#endif

/* Size of purgeable memory chunks expressed as the number of vm pages */
#ifndef PURGEABLE_CHUNK_VMPAGES
# define PURGEABLE_CHUNK_VMPAGES             24
#endif

/* Size of the queue of chunks that have zero pinned pcache pages, the queue 
** is designed to reduce the frequency of calls to vm_purgable_control by 
** keeping a small queue of recently unpinned chunks due to the observed pattern
** of frequent transitions of recently unpinned to pinned chunks during 
** processing of selects matching many rows.
*/
#ifndef PURGEABLE_CHUNK_QUEUE_SIZE
# define PURGEABLE_CHUNK_QUEUE_SIZE          100
#endif

#define PURGEABLE_CHUNK_SIZE    (PURGEABLE_CHUNK_VMPAGES * 4096)
#define SZ_PAGEDATA_PREFIX      8

/*
** Setting pcache_force_purge to a non-zero value (either directly or via
** the environment variable SQLITE_FORCE_PCACHE_PURGE) will simulate 
** purging every time a memory chunk is unpinned, this is intended for testing 
** only.
*/
static int pcache_force_purge=0;

/*
** _purgeableCache_globalPurge is used to ensure only one thread will kick off a
** global purge of queud up purgable chunks
*/
static int32_t _purgeableCache_globalPurge=0;

typedef enum {
  PinAction_NewPin = 0,    /* Pin page that wasn't used before */
  PinAction_RePin,         /* Pin page that is currently un-pinned */
  PinAction_Recycle,       /* Recycle and pin page that is currently unpinned */
  PinAction_Unpin,         /* Unpin page that is currently pinned */
  PinAction_Clear,         /* Unpin page and clear/invalidate data */
} PurgeablePinAction;

#if (TRACE_PURGEABLE_PCACHE>0)
static inline const char *_pinActionName(PurgeablePinAction action){
  static const char __pinactions[5][12] = { "New", "RePin", "Recycle", "Unpin", "Clear" };
  return __pinactions[action];
}
#endif

typedef struct PurgeablePCache PurgeablePCache;
typedef struct PgHdr1 PgHdr1;
typedef struct PgFreeslot PgFreeslot;
typedef struct PPCacheHdr PPCacheHdr;

/* Pointers to structures of this type are cast and returned as 
 ** opaque sqlite3_pcache* handles
 */
struct PurgeablePCache {
  /* Cache configuration parameters. Page size (szPage) and the purgeable
   ** flag (bPurgeable) are set when the cache is created. nMax may be 
   ** modified at any time by a call to the purgeableCacheCacheSize() method.
   ** The global mutex must be held when accessing nMax.
   */
  int szPage;                         /* Size of allocated pages in bytes */
  int bPurgeable;                     /* True if cache is purgeable */
  unsigned int nMin;                  /* Minimum number of pages reserved */
  unsigned int nMax;                  /* Configured "cache_size" value */
  unsigned int n90pct;                /* nMax*9/10 */

  /* Hash table of all pages. The following variables may only be accessed
  ** when the accessor is holding the global mutex 
  ** (see purgeableCacheEnterMutex() and purgeableCacheLeaveMutex()).
  */
  unsigned int nRecyclable;           /* Number of pages in the LRU list */
  unsigned int nPage;                 /* Total number of pages in apHash */
  unsigned int nHash;                 /* Number of slots in apHash[] */
  PgHdr1 **apHash;                    /* Hash table for fast lookup by key */
  PgHdr1 *pLruHead, *pLruTail;        /* LRU list of unpinned pages */

  unsigned int iMaxKey;               /* Largest key seen since xTruncate() */
  int szXPage;                        /* offset for hdr ptr + szPage */
  int nChunkPages;                    /* Number of pages per chunk */
  
  int nMaxChunks;                     /* Max number of chunks for cache_size */
  int nMaxUsedChunks;                 /* Max number of chunks used (set lazily) */
  int nChunks;                        /* Number of allocated chunks */
  void **apChunks;                    /* Array of purgeable blocks of pages */
  int *pChunkPinCount;                /* Count of pinned pages per chunk */
  int *pChunkUnpinCount;              /* Count of unpinned pages per chunk */
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  int aChunkQueue[PURGEABLE_CHUNK_QUEUE_SIZE]; /* Queue for unpinning */
#endif
};

/*
 ** Each cache entry is represented by an instance of the following 
 ** structure. A buffer of PgHdr1.pCache->szPage bytes is allocated 
 ** directly before this structure in memory (see the PGHDR1_TO_PAGE() 
 ** macro below).  However, if ENABLE_PURGABLE_CHUNKS is true, the page data  
 ** lives in the purgeable malloc zone and is pointed at by pPage.
 */
struct PgHdr1 {
  unsigned int iKey;             /* Key value (page number) */
  PgHdr1 *pNext;                 /* Next in hash table chain */
  PurgeablePCache *pCache;               /* Cache that currently owns this page */
  PgHdr1 *pLruNext;              /* Next in LRU list of unpinned pages */
  PgHdr1 *pLruPrev;              /* Previous in LRU list of unpinned pages */
  void *pPageData;               /* Pointer to page data */
  int iChunk;                    /* Index of the chunk */
};

/*
 ** Free slots in the allocator used to divide up the buffer provided using
 ** the SQLITE_CONFIG_PAGECACHE mechanism.
 */
struct PgFreeslot {
  PgFreeslot *pNext;  /* Next free slot */
};

struct PPCacheHdr {
  PPCacheHdr *pNext;
  PurgeablePCache *pCache;
};

/*
 ** Global data used by this cache.
 */
static SQLITE_WSD struct PCacheGlobal {
  sqlite3_mutex *mutex;               /* static mutex MUTEX_STATIC_LRU */

  /* these are just for internal bookeeping for the unit tests */
#ifdef SQLITE_TEST
  int nMaxPage;                       /* Sum of nMaxPage for purgeable caches */
  int nMinPage;                       /* Sum of nMinPage for purgeable caches */
  int nCurrentPage;                   /* Number of purgeable pages allocated */
  int nRecyclable;                    /* Number of purgeable pages allocated */
#endif
  
  /* Variables related to SQLITE_CONFIG_PAGECACHE settings. */
  int szSlot;                         /* Size of each free slot */
  void *pStart, *pEnd;                /* Bounds of pagecache malloc range */
  PgFreeslot *pFree;                  /* Free page blocks */
  int isInit;                         /* True if initialized */
  
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  /* Variable related to global purging */
  PPCacheHdr *pCacheHead;             /* Linked list of purgeableCaches */
#endif
} purgeableCache_g;

/*
 ** All code in this file should access the global structure above via the
 ** alias "purgeableCache". This ensures that the WSD emulation is used when
 ** compiling for systems that do not support real WSD.
 */
#define purgeableCache (GLOBAL(struct PCacheGlobal, purgeableCache_g))

/*
 ** When a PgHdr1 structure is allocated, the associated PurgeablePCache.szPage
 ** bytes of data are located directly before it in memory (i.e. the total
 ** size of the allocation is sizeof(PgHdr1)+PurgeablePCache.szPage byte). The
 ** PGHDR1_TO_PAGE() macro takes a pointer to a PgHdr1 structure as
 ** an argument and returns a pointer to the associated block of szPage
 ** bytes. The PAGE_TO_PGHDR1() macro does the opposite: its argument is
 ** a pointer to a block of szPage bytes of data and the return value is
 ** a pointer to the associated PgHdr1 structure.
 **
 **   assert( PGHDR1_TO_PAGE(PAGE_TO_PGHDR1(pCache, X))==X );
 */
#define PGHDR1_TO_PAGE(p) (void *)((p->pPageData) + SZ_PAGEDATA_PREFIX)
#define PAGE_TO_PGHDR1(p) *((PgHdr1 **)((void *)p - SZ_PAGEDATA_PREFIX))

/* Non-purgable variants for in-memory database support */
#define NP_PGHDR1_TO_PAGE(p)    (void*)(((char*)p) - p->pCache->szPage)
#define NP_PAGE_TO_PGHDR1(c, p) (PgHdr1*)(((char*)p) + c->szPage)

/*
 ** Macros to enter and leave the global LRU mutex.
 */
#define purgeableCacheEnterMutex() sqlite3_mutex_enter(purgeableCache.mutex)
#define purgeableCacheLeaveMutex() sqlite3_mutex_leave(purgeableCache.mutex)

static int purgeableCacheRetainChunk(PurgeablePCache *pCache, int iChunk, PurgeablePinAction pinAction, int *pPurged);
static int purgeableCacheReleaseChunk(PurgeablePCache *pCache, int iChunk, PurgeablePinAction pinAction);

/******************************************************************************/
/******** Page Allocation/SQLITE_CONFIG_PCACHE Related Functions **************/

/*
 ** This function is called during initialization if a static buffer is 
 ** supplied to use for the page-cache by passing the SQLITE_CONFIG_PAGECACHE
 ** verb to sqlite3_config(). Parameter pBuf points to an allocation large
 ** enough to contain 'n' buffers of 'sz' bytes each.
 */
void sqlite3PCacheBufferSetup(void *pBuf, int sz, int n){
  if( purgeableCache.isInit ){
    PgFreeslot *p;
    sz = ROUNDDOWN8(sz);
    purgeableCache.szSlot = sz;
    purgeableCache.pStart = pBuf;
    purgeableCache.pFree = 0;
    while( n-- ){
      p = (PgFreeslot*)pBuf;
      p->pNext = purgeableCache.pFree;
      purgeableCache.pFree = p;
      pBuf = (void*)&((char*)pBuf)[sz];
    }
    purgeableCache.pEnd = pBuf;
  }
}

/*
 ** Malloc function used within this file to allocate space from the buffer
 ** configured using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no 
 ** such buffer exists or there is no space left in it, this function falls 
 ** back to sqlite3Malloc().
 */
static void *purgeableCacheAlloc(int nByte){
  void *p;
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  sqlite3StatusSet(SQLITE_STATUS_PAGECACHE_SIZE, nByte);
  if( nByte<=purgeableCache.szSlot && purgeableCache.pFree ){
    assert( purgeableCache.isInit );
    p = (PgHdr1 *)purgeableCache.pFree;
    purgeableCache.pFree = purgeableCache.pFree->pNext;
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, 1);
  }else{
    
    /* Allocate a new buffer using sqlite3Malloc. Before doing so, exit the
     ** global pcache mutex and unlock the pager-cache object pCache. This is 
     ** so that if the attempt to allocate a new buffer causes the the 
     ** configured soft-heap-limit to be breached, it will be possible to
     ** reclaim memory from this pager-cache.
     */
    purgeableCacheLeaveMutex();
    p = sqlite3Malloc(nByte);
    purgeableCacheEnterMutex();
    if( p ){
      int sz = sqlite3MallocSize(p);
      sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, sz);
    }
  }
  sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
  return p;
}

/*
 ** Free an allocated buffer obtained from purgeableCacheAlloc().
 */
static void purgeableCacheFree(void *p){
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  if( p==0 ) return;
  if( p>=purgeableCache.pStart && p<purgeableCache.pEnd ){
    PgFreeslot *pSlot;
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, -1);
    pSlot = (PgFreeslot*)p;
    pSlot->pNext = purgeableCache.pFree;
    purgeableCache.pFree = pSlot;
  }else{
    int iSize;
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    iSize = sqlite3MallocSize(p);
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, -iSize);
    sqlite3_free(p);
  }
}

/* Returns the memory chunk at the specified index, allocating it if necessary 
** if iChunk is greater than the max number of chunks configured, then add 
** more chunks to avoid returning nonmem - the page cache should only request 
** chunks beyond the pCache->nMaxChunk limit if all other options have been
** exhausted. 
*/
static void *purgeableCacheGetChunk(PurgeablePCache *pCache, int iChunk){
  int nAllocatedChunks = pCache->nChunks;
  int nMaxChunks = pCache->nMaxChunks;
  
  if( nMaxChunks<=iChunk ){
    nMaxChunks = iChunk+1;
  }
  
  assert( sqlite3_mutex_held(purgeableCache.mutex) );

#if (TRACE_PURGEABLE_PCACHE>2)
  fprintf(stderr, "[PCACHE] .GETCHUNK 0x%x iChunk=%d nMin=%d nMax=%d nMaxChunks=%d[%d] (count %d) p->nMaxChunks=%d\n", 
          SQLITE_PTR_TO_INT(pCache), iChunk, pCache->nMin, pCache->nMax, pCache->nMaxChunks, nMaxChunks, nAllocatedChunks, pCache->nMaxUsedChunks);
#endif
  /* always grow the pCache chunk array pointers to nMaxChunks */
  if( nMaxChunks > pCache->nMaxUsedChunks ){
    int *pChunkPinCount = (int *)sqlite3_realloc(pCache->pChunkPinCount, sizeof(int *) * nMaxChunks);
    int *pChunkUnpinCount = (int *)sqlite3_realloc(pCache->pChunkUnpinCount, sizeof(int *) * nMaxChunks);
    void **apChunks = (void **)sqlite3_realloc(pCache->apChunks, sizeof(void *) * nMaxChunks);

    if( NULL == apChunks || NULL == pChunkPinCount || NULL == pChunkUnpinCount ){
#if (TRACE_PURGEABLE_PCACHE>0)
      fprintf(stderr, "reallocating chunks failed: %ld %ld %ld (from %d to %d)\n", (long)apChunks, (long)pChunkPinCount, (long)pChunkUnpinCount, pCache->nMaxUsedChunks, nMaxChunks);
#endif
      /* update any relocated pCache pointers and return NULL */
      if( NULL!=apChunks ){ 
        pCache->apChunks = apChunks; 
      }
      if( NULL!=pChunkPinCount ){ 
        pCache->pChunkPinCount = pChunkPinCount; 
      }
      if( NULL!=pChunkUnpinCount ){ 
        pCache->pChunkUnpinCount = pChunkUnpinCount; 
      }
      return NULL;
    }
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .GETCHUNK 0x%x realloc space for p->nMaxChunks=%d (was %d)\n", 
            SQLITE_PTR_TO_INT(pCache), pCache->nMaxChunks, pCache->nMaxUsedChunks);
#endif
    pCache->apChunks = apChunks;
    pCache->pChunkPinCount = pChunkPinCount;
    pCache->pChunkUnpinCount = pChunkUnpinCount;
    pCache->nMaxUsedChunks = nMaxChunks;
  }else if( nAllocatedChunks > pCache->nMaxChunks ){
    /* dealloc any chunks that are past the number of chunks needed for the
    ** number of pages in the cache, but only if there aren't any pages still 
    ** in use on them 
    */
    int i;
    for( i=(nAllocatedChunks-1); i>=pCache->nMaxChunks; i--){
      if( pCache->pChunkUnpinCount[i]==0 && pCache->pChunkPinCount[i]==0 ){
        vm_address_t vm_addr = (vm_address_t)pCache->apChunks[i];
        kern_return_t err = vm_deallocate(mach_task_self(), vm_addr, PURGEABLE_CHUNK_SIZE);
        if( err ){
          fprintf(stderr, "error returned from vm_deallocate: %d\n", err);
          break;
        }
#if (TRACE_PURGEABLE_PCACHE > 0)
        fprintf(stderr, "[PCACHE] .GETCHUNK 0x%x iChunk=%d vm_deallocate addr=0x%x\n", SQLITE_PTR_TO_INT(pCache), i, SQLITE_PTR_TO_INT(vm_addr));
#endif        
        pCache->apChunks[i] = NULL;
        pCache->nChunks = i;
        
      }else{
#if (TRACE_PURGEABLE_PCACHE > 0)
        fprintf(stderr, "[PCACHE] .GETCHUNK 0x%x iChunk=%d can't vm_deallocate - has references (pin=%d/unpin=%d)\n", 
                SQLITE_PTR_TO_INT(pCache), i, pCache->pChunkPinCount[i], pCache->pChunkUnpinCount[i]);
#endif        
        break;
      }
    }
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
    /* strip the deallocated chunks from the queue of pending chunks */
    {
      int j, k;
      for( j=0, k=0; j<PURGEABLE_CHUNK_QUEUE_SIZE; j++ ){
        if( pCache->aChunkQueue[j] == -1){
          break;
        }else if( pCache->aChunkQueue[j] < pCache->nChunks){
          pCache->aChunkQueue[k] = pCache->aChunkQueue[j];
          k++;
        }
      }
      for(; k<PURGEABLE_CHUNK_QUEUE_SIZE; k++) {
        pCache->aChunkQueue[k] = -1;
      }
    }
#endif
    
  }
  
  if( iChunk >= nAllocatedChunks ){
    /* allocate more purgable chunks on demand */
    vm_map_t task_self = mach_task_self();
#ifndef OMIT_VM_PURGABLE
    int vm_flags = VM_FLAGS_ANYWHERE | VM_FLAGS_PURGABLE | VM_MAKE_TAG(VM_MEMORY_SQLITE);
#else
    int vm_flags = VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_SQLITE);
#endif
    int i;
    
    /* allocate the space for the chunks up to iChunk */
    for( i = nAllocatedChunks; i <= iChunk; i++ ){
      vm_address_t vm_addr = 0;
      kern_return_t rv = vm_allocate(task_self, &vm_addr, (size_t)PURGEABLE_CHUNK_SIZE, vm_flags);
      if( rv ){
#if (TRACE_PURGEABLE_PCACHE > 0)
        fprintf(stderr, "vm_allocate failed on chunk %d during allocs from %d to %d: %d\n", i, nAllocatedChunks, iChunk, rv);
#endif
        return NULL;
      }
      pCache->apChunks[i] = (void *)vm_addr;
      pCache->pChunkPinCount[i] = 0;
      pCache->pChunkUnpinCount[i] = 0;
      pCache->nChunks = (i+1);
#if (TRACE_PURGEABLE_PCACHE > 0)
      fprintf(stderr, "[PCACHE] .GETCHUNK 0x%x vm_allocate chunk %d addr = 0x%x\n", 
              SQLITE_PTR_TO_INT(pCache), i, SQLITE_PTR_TO_INT(vm_addr));
#endif      
    }
    
  }
  return pCache->apChunks[iChunk];
}
#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
** Return the size of a pache allocation
*/
static int purgeableCacheMemSize(void *p){
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  if( p>=purgeableCache.pStart && p<purgeableCache.pEnd ){
    return purgeableCache.szSlot;
  }else{
    int iSize;
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    iSize = sqlite3MallocSize(p);
    sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
    return iSize;
  }
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

/*
 ** Allocate a new page object initially associated with cache pCache.  
 */
static PgHdr1 *purgeableCacheAllocPage(PurgeablePCache *pCache){
  /* ACS: route allocations from our purgable chunks */
  int nByte = sizeof(PgHdr1);
  int i, iChunk;  
  void *pChunk = NULL;
  void *pPg = NULL;
  PgHdr1 *p = NULL;
  int bPurgeable = pCache->bPurgeable;

  assert( sqlite3_mutex_held(purgeableCache.mutex) );

  if( !bPurgeable ){
    nByte += pCache->szPage;
  }
  pPg = purgeableCacheAlloc(nByte);
  if( !pPg ){
    return NULL;
  }
  
  if( bPurgeable ){
    void *pPageData = NULL;
    int foundPageData = 0;
#ifdef SQLITE_TEST
    purgeableCache.nCurrentPage++;
#endif
    p = (PgHdr1 *)pPg;    
    /* find the first available page data */
    p->pPageData = NULL;
    /* search through chunks for unused page data */
    for( iChunk = 0; !foundPageData; iChunk++ ){
      pChunk = purgeableCacheGetChunk(pCache, iChunk);
      if( pChunk==NULL ){
        purgeableCacheFree(p);
        return NULL;
      }
      if( ((pCache->pChunkPinCount[iChunk] + pCache->pChunkUnpinCount[iChunk]) ==
           0 )){
        /* special case - if the chunk has zero pinned and zero unpinned pages
         it needs to be marked non-purgable before acessing it */
        if( purgeableCacheRetainChunk(pCache, iChunk, PinAction_NewPin, NULL) ){
          purgeableCacheFree(p);
          return NULL;
        }
        pPageData=pChunk;
        assert( *(PgHdr1 **)pPageData == NULL );
        foundPageData = 1;
      }else if( (pCache->pChunkPinCount[iChunk] + pCache->pChunkUnpinCount[iChunk]) <
               pCache->nChunkPages ){
        for( i=0; i<pCache->nChunkPages; i++ ){
          pPageData=(pChunk + (i * pCache->szXPage));
          if( *(PgHdr1 **)pPageData == NULL ){
            if( purgeableCacheRetainChunk(pCache, iChunk, PinAction_NewPin, NULL) ){
              purgeableCacheFree(p);
              return NULL;
            }
            foundPageData = 1;
            break;
          }
        }
      }
      if( foundPageData ){
        p->pPageData = pPageData;
        p->iChunk = iChunk;
        *(PgHdr1 **)pPageData = p;
        sqlite3StatusAdd(SQLITE_STATUS_MEMORY_USED, pCache->szPage);
        sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, pCache->szPage);
#if (TRACE_PURGEABLE_PCACHE > 1)
        fprintf(stderr, "[PCACHE] .ALLOCPG  SQLITE_STATUS_MEMORY_USED+=%d\n", pCache->szXPage);
#endif
      }
    }
    if( !pChunk || !p->pPageData ){
      purgeableCacheFree(p);
      p = NULL;
    }
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .ALLOCPG  0x%x iChunk=%d addr=0x%x\n", 
            SQLITE_PTR_TO_INT(pCache), (p ? p->iChunk : -1), (p ? SQLITE_PTR_TO_INT(p->pPageData) : 0));
#endif      
  } else { 
    /* !bPurgeable */
    p = NP_PAGE_TO_PGHDR1(pCache, pPg);
  }
  
  return p;
}

/*
 ** Free a page object allocated by purgeableCacheAllocPage().
 **
 ** The pointer is allowed to be NULL, which is prudent.  But it turns out
 ** that the current implementation happens to never call this routine
 ** with a NULL pointer, so we mark the NULL test with ALWAYS(). 
 */
static void purgeableCacheFreePage(PgHdr1 *p){
#if (TRACE_PURGEABLE_PCACHE > 0)
  if (p) { fprintf(stderr, "[PCACHE] .FREEPG   0x%x iChunk=%d addr=0x%x\n", SQLITE_PTR_TO_INT(p->pCache), p->iChunk, SQLITE_PTR_TO_INT(p->pPageData)); 
  } else { fprintf(stderr, "[PCACHE] .FREEPG   0x0 iChunk=-1 addr=0x0\n"); }
#endif      
  if( ALWAYS(p) ){
    if( p->pCache->bPurgeable ){
#ifdef SQLITE_TEST
      purgeableCache.nCurrentPage--;
#endif
      if( p->pPageData ){
        *(void **)p->pPageData = NULL;
        sqlite3StatusAdd(SQLITE_STATUS_MEMORY_USED, -1*p->pCache->szPage);
        sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, -1*p->pCache->szPage);
#if (TRACE_PURGEABLE_PCACHE > 1)
        fprintf(stderr, "[PCACHE] .FREEPG   SQLITE_STATUS_MEMORY_USED-=%d\n", p->pCache->szXPage);
#endif
      }
      purgeableCacheFree(p);
    } else {
      purgeableCacheFree(NP_PGHDR1_TO_PAGE(p));
    }
  }
}

/*
 ** Malloc function used by SQLite to obtain space from the buffer configured
 ** using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no such buffer
 ** exists, this function falls back to sqlite3Malloc().
 */
void *sqlite3PageMalloc(int sz){
  void *p;
  purgeableCacheEnterMutex();
  p = purgeableCacheAlloc(sz);
  purgeableCacheLeaveMutex();
  return p;
}

/*
 ** Free an allocated buffer obtained from sqlite3PageMalloc().
 */
void sqlite3PageFree(void *p){
  purgeableCacheEnterMutex();
  purgeableCacheFree(p);
  purgeableCacheLeaveMutex();
}

/******************************************************************************/
/******** General Implementation Functions ************************************/

/*
 ** This function is used to resize the hash table used by the cache passed
 ** as the first argument.
 **
 ** The global mutex must be held when this function is called.
 */
static int purgeableCacheResizeHash(PurgeablePCache *p){
  PgHdr1 **apNew;
  unsigned int nNew;
  unsigned int i;
    
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  
  nNew = p->nHash*2;
  if( nNew<256 ){
    nNew = 256;
  }
  
  purgeableCacheLeaveMutex();
  if( p->nHash ){ sqlite3BeginBenignMalloc(); }
  apNew = (PgHdr1 **)sqlite3_malloc(sizeof(PgHdr1 *)*nNew);
  if( p->nHash ){ sqlite3EndBenignMalloc(); }
  purgeableCacheEnterMutex();
  if( apNew ){
    memset(apNew, 0, sizeof(PgHdr1 *)*nNew);
    for(i=0; i<p->nHash; i++){
      PgHdr1 *pPage;
      PgHdr1 *pNext = p->apHash[i];
      while( (pPage = pNext)!=0 ){
        unsigned int h = pPage->iKey % nNew;
        pNext = pPage->pNext;
        pPage->pNext = apNew[h];
        apNew[h] = pPage;
      }
    }
    sqlite3_free(p->apHash);
    p->apHash = apNew;
    p->nHash = nNew;
  }
  
  return (p->apHash ? SQLITE_OK : SQLITE_NOMEM);
}

/*
 ** This function is used internally to remove the page pPage from the 
 ** LRU list, if is part of it. If pPage is not part of the
 ** LRU list, then this function is a no-op.
 **
 ** The global mutex must be held when this function is called.
 */
static void purgeableCachePinPage(PgHdr1 *pPage){
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  if( pPage && (pPage->pLruNext || pPage==pPage->pCache->pLruTail) ){
    if( pPage->pLruPrev ){
      pPage->pLruPrev->pLruNext = pPage->pLruNext;
    }
    if( pPage->pLruNext ){
      pPage->pLruNext->pLruPrev = pPage->pLruPrev;
    }
    if( pPage->pCache->pLruHead==pPage ){
      pPage->pCache->pLruHead = pPage->pLruNext;
    }
    if( pPage->pCache->pLruTail==pPage ){
      pPage->pCache->pLruTail = pPage->pLruPrev;
    }
    pPage->pLruNext = 0;
    pPage->pLruPrev = 0;
    pPage->pCache->nRecyclable--;
#ifdef SQLITE_TEST
    purgeableCache.nRecyclable--;
#endif
  }
}


/*
 ** Remove the page supplied as an argument from the hash table 
 ** (PurgeablePCache.apHash structure) that it is currently stored in.
 **
 ** The global mutex must be held when this function is called.
 */
static void purgeableCacheRemoveFromHash(PgHdr1 *pPage){
  unsigned int h;
  PurgeablePCache *pCache = pPage->pCache;
  PgHdr1 **pp;

  assert( sqlite3_mutex_held(purgeableCache.mutex) );

  h = pPage->iKey % pCache->nHash;
  for(pp=&pCache->apHash[h]; (*pp)!=pPage; pp=&(*pp)->pNext);
  *pp = (*pp)->pNext;
  
  pCache->nPage--;
}

/* Decrement the pin count on the chunk (based on action). 
*/
static int purgeableCacheReleaseChunk(PurgeablePCache *pCache, int iChunk, PurgeablePinAction action){  
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  assert( action == PinAction_Unpin || action == PinAction_Clear );
  
  if( pCache->pChunkPinCount[iChunk] == 1 ){
    /* unpin the chunk if the pin count is going from 1 to 0 */
    int xChunk = iChunk;
    kern_return_t err = 0;

#ifndef OMIT_VM_PURGABLE
    int state = VM_PURGABLE_VOLATILE;
    void *pChunk = NULL;

#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
    /* insert iChunk into the queue, if the queue was full, then mark the 
     * last chunk (the one that is pushed out) as purgeable 
     */
    int j;
    xChunk = pCache->aChunkQueue[PURGEABLE_CHUNK_QUEUE_SIZE-1];
    for( j=(PURGEABLE_CHUNK_QUEUE_SIZE-1); j>0; j-- ){
      pCache->aChunkQueue[j] = pCache->aChunkQueue[j-1];
    }
    pCache->aChunkQueue[0]=iChunk;
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .RLSCHUNK 0x%x iChunk=%d added to aChunkQueue\n", SQLITE_PTR_TO_INT(pCache), iChunk);
#endif
#if (TRACE_PURGEABLE_PCACHE > 3)
    {
      int tiCh;
      fprintf(stderr, "aChunkQueue=[ ");
      for( tiCh=0; tiCh<PURGEABLE_CHUNK_QUEUE_SIZE; tiCh++ ){
        fprintf(stderr, "%d ", pCache->aChunkQueue[tiCh]);
      }
      fprintf(stderr, "]\n");
    }
#endif
#endif /* PURGEABLE_CHUNK_QUEUE_SIZE */
    
    if( xChunk!=-1 ){
      pChunk = pCache->apChunks[xChunk];
      err = vm_purgable_control(mach_task_self(), (vm_address_t)pChunk, VM_PURGABLE_SET_STATE, &state);
      /* fprintf(stderr, "marking pChunk[%d] (%p - %p, sz=%d KB) as volatile [%d]\n", iChunk, pChunk, pChunk + PURGEABLE_CHUNK_SIZE, (int)PURGEABLE_CHUNK_SIZE/1024, err); */
    }
#endif /* OMIT_VM_PURGABLE */

#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .RLSCHUNK 0x%x iChunk=%d, xChunk=%d vm_purgable_control(%s)\n", SQLITE_PTR_TO_INT(pCache), iChunk, xChunk, (xChunk==-1) ? "NOP" : "set-volatile");
#endif
    if( err!=0 ){ 
      return err;
    }
  }    
  pCache->pChunkPinCount[iChunk] -= 1;
  if( action == PinAction_Unpin ){
    pCache->pChunkUnpinCount[iChunk] += 1;
  }      
  
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] .RLSCHUNK 0x%x iChunk=%d pinCount=%d unpinCount=%d action=%s\n", SQLITE_PTR_TO_INT(pCache), iChunk, pCache->pChunkPinCount[iChunk], pCache->pChunkUnpinCount[iChunk], _pinActionName(action));
#endif
  
  assert( pCache->pChunkUnpinCount[iChunk] >= 0 );
  assert( pCache->pChunkPinCount[iChunk] >=0 );
  assert( (pCache->pChunkUnpinCount[iChunk] + pCache->pChunkPinCount[iChunk]) <= pCache->nChunkPages);
  
  return 0;
}

/* Returns pPurged=EFAULT when the request to retain the chunk pinned memory that had 
** previously been purged 
*/
static int purgeableCacheRetainChunk(
  PurgeablePCache *pCache, 
  int iChunk, 
  PurgeablePinAction action,
  int *pPurged
){  
  int unpinnedPurged = 0;
  kern_return_t err = 0;
  void *pChunk = pCache->apChunks[iChunk];
  
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  assert( action==PinAction_NewPin || action==PinAction_RePin || 
         action==PinAction_Recycle );
  
  /* if this is the first pin then update the chunk purgable status */
  if( pCache->pChunkPinCount[iChunk] == 0 ){
    int state = VM_PURGABLE_NONVOLATILE;
    int xChunk = iChunk;

#ifndef OMIT_VM_PURGABLE

#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
    /* remove the retained chunk from the queue of chunks pending purge */
    int j, k;
    for( j=0, k=0; j<PURGEABLE_CHUNK_QUEUE_SIZE; j++ ){
      if( pCache->aChunkQueue[j] == -1){
        break;
      }else if( pCache->aChunkQueue[j] == iChunk){
        xChunk=-1; 
        /* iChunk was in the queue and hadn't been marked volatile so 
         * remove it from the queue and don't mark it non-volatile */
#if (TRACE_PURGEABLE_PCACHE > 0)
        fprintf(stderr, "[PCACHE] .RETCHUNK 0x%x iChunk=%d removed from aChunkQueue (pos %d)\n", SQLITE_PTR_TO_INT(pCache), iChunk, j);
#endif
      }else{
        pCache->aChunkQueue[k] = pCache->aChunkQueue[j];  
        k++;
      }
    }
    for(; k<PURGEABLE_CHUNK_QUEUE_SIZE; k++) {
      pCache->aChunkQueue[k] = -1;
    }
#if (TRACE_PURGEABLE_PCACHE > 3)
    {
      int ti;
      fprintf(stderr, "aChunkQueue=[ ");
      for( ti=0; ti<PURGEABLE_CHUNK_QUEUE_SIZE; ti++ ){
        fprintf(stderr, "%d ", pCache->aChunkQueue[ti]);
      }
      fprintf(stderr, " ]\n");
    }
#endif
#endif /* PURGEABLE_CHUNK_QUEUE_SIZE */
    if( xChunk!=-1 ){
      err = vm_purgable_control(mach_task_self(), (vm_address_t)pChunk, VM_PURGABLE_SET_STATE, &state);
      /* fprintf(stderr, "marking pChunk[%d] (%p - %p, sz=%d KB) as nonvolatile [%d]\n", iChunk, pChunk, pChunk + PURGEABLE_CHUNK_SIZE, (int)PURGEABLE_CHUNK_SIZE/1024, err); */
      if( pcache_force_purge>0 ){
        state = VM_PURGABLE_EMPTY;
      }
    }
#endif /* OMIT_VM_PURGABLE */
    
    if( err!=0 ){
      return err;
    }
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .RETCHUNK 0x%x iChunk=%d xChunk=%d vm_purgable_control(%s)\n", SQLITE_PTR_TO_INT(pCache), iChunk, xChunk, (xChunk==-1) ? "NOP" : "set-nonvolatile");
#endif
    
    if( pPurged ){
      *pPurged=(state == VM_PURGABLE_EMPTY) ? EFAULT : 0;
    }
    /* if chunk was purged, zero it and delete unpinned pages */
    if( (state==VM_PURGABLE_EMPTY) && (pCache->pChunkUnpinCount[iChunk]>0) ){
      PgHdr1 *pPage=pCache->pLruHead;
      int unpinTarget = (action == PinAction_Recycle) ? 1 : 0;

      unpinnedPurged = 1;
      bzero(pChunk, PURGEABLE_CHUNK_SIZE);
      while( pPage!=NULL && (pCache->pChunkUnpinCount[iChunk] > unpinTarget) ){
        PgHdr1 *pNext = pPage->pLruNext;
        if( pPage->iChunk == iChunk){
#if (TRACE_PURGEABLE_PCACHE > 0)
          fprintf(stderr, "[PCACHE] .RETCHUNK 0x%x iChunk=%d purged, clearing page %d\n", SQLITE_PTR_TO_INT(pCache), iChunk, pPage->iKey);
#endif
          purgeableCachePinPage(pPage); /* remove from LRU list */
          purgeableCacheRemoveFromHash(pPage); 
          purgeableCacheFreePage(pPage);
          pCache->pChunkUnpinCount[iChunk] -= 1;
        }
        pPage = pNext;
      }
      /* if we're recycling, there should be one unpinned page missing from the 
      ** LRU list, leaving an unpin count of 1 here (it's decremented below) */
      assert( pCache->pChunkUnpinCount[iChunk]==unpinTarget );
    }
  }
  
  /* If this is a re-pin and iChunk had its unpinned pages purged, then 
   *
   * (a) it could be marked as volatile since there isn't any valid unpinned 
   * data on it now, however for performance reasons we avoid the system call 
   * to mark the chunk as volatile.  The only downside to this is that we're leaving 
   * a chunk non-volatile when it could be safely reclaimed by the vm system.
   * A worthwhile tradeoff.  
   *
   * Also, (b) there is no need to adjust the pin or unpin count on the chunk, 
   * since the repin effectively failed.
   */
  if( !((action == PinAction_RePin) && unpinnedPurged) ){
    pCache->pChunkPinCount[iChunk] += 1;
    if( action == PinAction_RePin || action == PinAction_Recycle ){
      pCache->pChunkUnpinCount[iChunk] -= 1;
    }
  }
  
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] .RETCHUNK 0x%x iChunk=%d pinCount=%d unpinCount=%d action=%s (unpinnedPurged=%d)\n", SQLITE_PTR_TO_INT(pCache), iChunk, pCache->pChunkPinCount[iChunk], pCache->pChunkUnpinCount[iChunk], _pinActionName(action), unpinnedPurged);
#endif
  
  assert( pCache->pChunkUnpinCount[iChunk] >= 0 );
  assert( pCache->pChunkPinCount[iChunk] >=0 );
  assert( (pCache->pChunkUnpinCount[iChunk] + pCache->pChunkPinCount[iChunk]) <= pCache->nChunkPages);
  
  return 0;
}

/*
 ** If there are currently more than pcache.nMaxPage pages allocated, try
 ** to recycle pages to reduce the number allocated to pcache.nMaxPage.
 */
static void purgeableCacheEnforceCacheMaxPage(PurgeablePCache *pCache){
  int bPurgeable = pCache->bPurgeable;
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  while( pCache->nPage > pCache->nMax && pCache->pLruTail ){
    PgHdr1 *p = pCache->pLruTail;
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .FORCEMAX 0x%x nPage=%d nMax=%d action=Clear\n", 
            SQLITE_PTR_TO_INT(pCache), pCache->nPage, pCache->nMax);
#endif
    if( bPurgeable ){
      /* the page was already unpinned, so decrement the unpinned count on 
       * chunk to reflect the transition of the page data from unpinned to
       * cleared. */
      pCache->pChunkUnpinCount[p->iChunk] -= 1;
    }
    purgeableCachePinPage(p);  /* remove from LRU list */
    purgeableCacheRemoveFromHash(p); 
    purgeableCacheFreePage(p);
  }
}

/*
 ** Discard all pages from cache pCache with a page number (key value) 
 ** greater than or equal to iLimit. Any pinned pages that meet this 
 ** criteria are unpinned before they are discarded.
 **
 ** The global mutex must be held when this function is called.
 */
static void purgeableCacheTruncateUnsafe(
  PurgeablePCache *pCache, 
  unsigned int iLimit 
){
  TESTONLY( unsigned int nPage=0; ) /* Used to assert pCache->nPage is correct*/
  unsigned int h;
  int bPurgeable = pCache->bPurgeable;
  assert( sqlite3_mutex_held(purgeableCache.mutex) );
  for(h=0; h<pCache->nHash; h++){
    PgHdr1 **pp = &pCache->apHash[h]; 
    PgHdr1 *pPage;
    while( (pPage = *pp)!=0 ){
      if( pPage->iKey>=iLimit ){
        if( bPurgeable && pPage->pPageData!=NULL ){
#if (TRACE_PURGEABLE_PCACHE>0)
          fprintf(stderr, "[PCACHE] .TRUNCATE 0x%x iLimit=%d iKey=%d PinAction_Clear\n", 
                  SQLITE_PTR_TO_INT(pCache), iLimit, pPage->iKey);
#endif          
          if( !(pPage->pLruNext || pPage==pPage->pCache->pLruTail) ){
            purgeableCacheReleaseChunk(pCache, pPage->iChunk, PinAction_Clear);
          }else{
            /* page is in the LRU list, so the page was already unpinned, 
             * decrement the unpinned count on the chunk to reflect the 
             * transition of the page data from unpinned to cleared. */
            pCache->pChunkUnpinCount[pPage->iChunk] -= 1;
          }
        }
        pCache->nPage--;
        *pp = pPage->pNext;
        purgeableCachePinPage(pPage);
        purgeableCacheFreePage(pPage);
      }else{
        pp = &pPage->pNext;
        TESTONLY( nPage++; )
      }
    }
  }
  assert( pCache->nPage==nPage );
}

#if (!defined(OMIT_VM_PURGABLE)) && (PURGEABLE_CHUNK_QUEUE_SIZE>0)

/* Handle jetsam notification by immediately marking all of the pages in the 
** queue volatile and removing them from the pending queue.
*/
static int purgeableCachePurgePendingChunks(PurgeablePCache *pCache){
  int state = VM_PURGABLE_VOLATILE;
  void *pChunk = NULL;  
  int j, iChunk, nChunks = 0;
  vm_map_t task = mach_task_self();

  if (NULL == pCache) {
    return 0;
  }
  assert( sqlite3_mutex_held(purgeableCache.mutex) );

  for( j=(PURGEABLE_CHUNK_QUEUE_SIZE-1); j>= 0; j-- ){
    iChunk = pCache->aChunkQueue[j];
    if( iChunk != -1){
      pChunk = pCache->apChunks[iChunk];
      state = VM_PURGABLE_VOLATILE;
      /*fprintf(stderr, "marking pChunk[%d] (%p - %p, sz=%d KB) as purgeable(%d)\n", j, pChunk, pChunk + PURGEABLE_CHUNK_SIZE, (int)PURGEABLE_CHUNK_SIZE/1024, state); */
      int err = vm_purgable_control(task, (vm_address_t)pChunk, VM_PURGABLE_SET_STATE, &state);
      /* log errors, but don't give up. */
      if( err!=KERN_SUCCESS ){
        fprintf(stderr, "error marking chunk volatile %d\n", errno);
        break; /* failing to mark a chunk volatile means clean up and give up */
      }
#if (TRACE_PURGEABLE_PCACHE > 3)
      int getState;
      err = vm_purgable_control(task, (vm_address_t)pChunk, VM_PURGABLE_GET_STATE, &getState);
      if( err!=KERN_SUCCESS ){
        fprintf(stderr, "error getting chunk status %d\n", errno);
        break; /* failing to mark a chunk volatile means clean up and give up */
      } else {
        fprintf(stderr, "got chunk status: %d=%s\n", getState, ((getState & VM_PURGABLE_STATE_MASK ) == VM_PURGABLE_VOLATILE) ? "volatile" : "not volatile");
      }
#endif
      pCache->aChunkQueue[j] = -1; 
      nChunks++;
    }
  }
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] .JETSAM 0x%x marked %d queued chunks vm_purgable_control(volatile)\n", (int)SQLITE_PTR_TO_INT(pCache), nChunks);
#endif
  return 0;
}

#endif /* (!defined(OMIT_VM_PURGABLE) && (PURGEABLE_CHUNK_QUEUE_SIZE>0) */

#if (SQLITE_ENABLE_APPLE_SPI>0)
void _sqlite3_purgeEligiblePagerCacheMemory(void);
void _sqlite3_purgeEligiblePagerCacheMemory(void){
# if (!defined(OMIT_VM_PURGABLE)) && (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  int go = OSAtomicCompareAndSwap32Barrier(0, 1, 
       (volatile int32_t *) &_purgeableCache_globalPurge); 
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] .JETSAM purge unpinned page chunks\n");
#endif
  if( go ){
    PPCacheHdr *pCacheHdr = NULL;
    purgeableCacheEnterMutex();
    for( pCacheHdr=purgeableCache.pCacheHead; pCacheHdr!=NULL; 
        pCacheHdr=pCacheHdr->pNext ){
      purgeableCachePurgePendingChunks(pCacheHdr->pCache);
    }
    OSAtomicCompareAndSwap32Barrier(1, 0, 
        (volatile int32_t *) &_purgeableCache_globalPurge);
    purgeableCacheLeaveMutex();    
  }
# endif
}

#endif /* SQLITE_ENABLE_APPLE_SPI */

/******************************************************************************/
/******** sqlite3_pcache Methods **********************************************/

/*
 ** Implementation of the sqlite3_pcache.xInit method.
 */
static int purgeableCacheInit(void *NotUsed){
  char *envforcepurge = getenv("SQLITE_FORCE_PCACHE_PURGE");
  UNUSED_PARAMETER(NotUsed);
  assert( purgeableCache.isInit==0 );
  
  /* SQLITE_FORCE_PCACHE_PURGE>=1 means force always purge the purgeable chunk.
  ** this is intended for testing only.
  */
  if( envforcepurge!=NULL ){
    pcache_force_purge = atoi(envforcepurge)>0;
  }
    
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] INIT%s\n", (pcache_force_purge>0 ? " (pcache_force_purge>0)":""));
#endif
  memset(&purgeableCache, 0, sizeof(purgeableCache));
  if( sqlite3GlobalConfig.bCoreMutex ){
    /* pcache1 has divided the LRU mutex from the PMEM to reduce contention, purgeable pcache uses the PMEM for both.  The adoption of both mutexes is tracked by <rdar://problem/8984374> Investigate adoption of pcache PGroup layer to reduce mutex usage
     purgeableCache.grp.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_LRU); */
    purgeableCache.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_PMEM);
  }
  purgeableCache.isInit = 1;
  return SQLITE_OK;
}

/*
 ** Implementation of the sqlite3_pcache.xShutdown method.
 ** Note that the static mutex allocated in xInit does 
 ** not need to be freed.
 */
static void purgeableCacheShutdown(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
#if (TRACE_PURGEABLE_PCACHE > 0)
  fprintf(stderr, "[PCACHE] SHUTDOWN\n");
#endif
  assert( purgeableCache.isInit!=0 );
  memset(&purgeableCache, 0, sizeof(purgeableCache));
}

/*
 ** Implementation of the sqlite3_pcache.xCreate method.
 **
 ** Allocate a new cache.
 */
static sqlite3_pcache *purgeableCacheCreate(int szPage, int bPurgeable){
  PurgeablePCache *pCache;
  
  pCache = (PurgeablePCache *)sqlite3_malloc(sizeof(PurgeablePCache));
  if( pCache ){
    memset(pCache, 0, sizeof(PurgeablePCache));
    pCache->szPage = szPage;
    /* make sure the data pointer is 8-byte aligned */
    pCache->szXPage = ((SZ_PAGEDATA_PREFIX + szPage + 7) / 8) * 8;
    pCache->nChunkPages = (PURGEABLE_CHUNK_SIZE / pCache->szXPage);
    pCache->bPurgeable = (bPurgeable ? 1 : 0);
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
    {
      int i;
      PPCacheHdr *pCacheHdr;
      
      for( i=0; i<PURGEABLE_CHUNK_QUEUE_SIZE; i++ ){
        pCache->aChunkQueue[i] = -1;
      }
      pCacheHdr = (PPCacheHdr *)sqlite3_malloc(sizeof(PPCacheHdr));
      if( pCacheHdr ){
        pCacheHdr->pCache = pCache;
      
        purgeableCacheEnterMutex();
        pCacheHdr->pNext = purgeableCache.pCacheHead;
        purgeableCache.pCacheHead=pCacheHdr;
        purgeableCacheLeaveMutex();
      }else{
        sqlite3_free(pCache);
        return NULL;
      }
    }
#endif
    if( bPurgeable ){
      pCache->nMin = (10 > pCache->nChunkPages) ? 10 : pCache->nChunkPages;
#ifdef SQLITE_TEST
      pCache->nMin = 10; /* unit tests depend on nMin = 10 */
      purgeableCacheEnterMutex();
      purgeableCache.nMinPage += pCache->nMin;
      purgeableCacheLeaveMutex();
#endif
    }
#if (TRACE_PURGEABLE_PCACHE >0)
    fprintf(stderr, "[PCACHE] CREATE    0x%x szPage=%d nChunkPages=%d bPurgeable=%d szChunkQueue=%d\n", SQLITE_PTR_TO_INT(pCache), pCache->szPage, pCache->nChunkPages, pCache->bPurgeable, PURGEABLE_CHUNK_QUEUE_SIZE);
#endif
  }
  return (sqlite3_pcache *)pCache;
}

/*
 ** Implementation of the sqlite3_pcache.xCachesize method. 
 **
 ** Configure the cache_size limit for a cache.
 */
static void purgeableCacheCachesize(sqlite3_pcache *p, int nMax){
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  if( pCache->bPurgeable ){
    int nPages=0;
    
    purgeableCacheEnterMutex();
#ifdef SQLITE_TEST
    purgeableCache.nMaxPage += (nMax - pCache->nMax);
#endif
#if (TRACE_PURGEABLE_PCACHE>0)
    fprintf(stderr, "[PCACHE] CACHESIZE 0x%x nMin=%d nMax=%d (was %d)\n", 
            SQLITE_PTR_TO_INT(pCache), pCache->nMin, nMax, pCache->nMax);
#endif
    pCache->nMax = nMax;
    pCache->n90pct = pCache->nMax*9/10;
    nPages = (nMax > pCache->nMin) ? nMax : pCache->nMin;
    pCache->nMaxChunks = ((nPages - 1) / pCache->nChunkPages) + 1;
    purgeableCacheEnforceCacheMaxPage(pCache);

    purgeableCacheLeaveMutex();
  }
}

/*
 ** Implementation of the sqlite3_pcache.xPagecount method. 
 */
static int purgeableCachePagecount(sqlite3_pcache *p){
  int n;
  purgeableCacheEnterMutex();
  n = ((PurgeablePCache *)p)->nPage;
#if (TRACE_PURGEABLE_PCACHE>2)
  fprintf(stderr, "[PCACHE] PAGECOUNT 0x%x n=%d\n", SQLITE_PTR_TO_INT(p), n);
#endif
  purgeableCacheLeaveMutex();
  return n;
}

/*
 ** Implementation of the sqlite3_pcache.xFetch method. 
 **
 ** Fetch a page by key value.
 **
 ** Whether or not a new page may be allocated by this function depends on
 ** the value of the createFlag argument.  0 means do not allocate a new
 ** page.  1 means allocate a new page if space is easily available.  2 
 ** means to try really hard to allocate a new page.
 **
 ** For a non-purgeable cache (a cache used as the storage for an in-memory
 ** database) there is really no difference between createFlag 1 and 2.  So
 ** the calling function (pcache.c) will never have a createFlag of 1 on
 ** a non-purgable cache.
 **
 ** There are three different approaches to obtaining space for a page,
 ** depending on the value of parameter createFlag (which may be 0, 1 or 2).
 **
 **   1. Regardless of the value of createFlag, the cache is searched for a 
 **      copy of the requested page. If one is found, it is returned.
 **
 **   2. If createFlag==0 and the page is not already in the cache, NULL is
 **      returned.
 **
 **   3. If createFlag is 1, and the page is not already in the cache, 
 **      and if the following is true, return NULL:
 **
 **       (a) the number of pages pinned by the cache is greater than nMax
 **
 **   4. If none of the first three conditions apply and the cache is marked
 **      as purgeable, and if one of the following is true:
 **
 **       (a) The number of pages allocated for the cache is already 
 **           PurgeablePCache.nMax
 **
 **      then attempt to recycle a page from the LRU list and return the 
 **      recycled buffer.
 **
 **   5. Otherwise, allocate and return a new page buffer.
 */
static void *purgeableCacheFetch(sqlite3_pcache *p, unsigned int iKey, int createFlag){
  unsigned int nPinned;
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  PgHdr1 *pPage = 0;
  int pagePurged = 0;
  int bPurgeable = pCache->bPurgeable;

  assert( bPurgeable || createFlag!=1 );
  purgeableCacheEnterMutex();
  if( createFlag==1 ) sqlite3BeginBenignMalloc();
  
  /* Search the hash table for an existing entry. */
  if( pCache->nHash>0 ){
    unsigned int h = iKey % pCache->nHash;
    for(pPage=pCache->apHash[h]; pPage&&pPage->iKey!=iKey; pPage=pPage->pNext);
  }
  
#if (TRACE_PURGEABLE_PCACHE>1)
  fprintf(stderr, "[PCACHE] FETCH     0x%x iKey=%d create=%d page=0x%x\n", 
          SQLITE_PTR_TO_INT(pCache), iKey, createFlag, SQLITE_PTR_TO_INT(pPage));
#endif
  if( pPage && (pPage->pLruNext || pPage==pPage->pCache->pLruTail) ){
    /* if unpinned, attempt to pin it if this is a purgeable cache */
    if( bPurgeable ){
      if( purgeableCacheRetainChunk(pPage->pCache, pPage->iChunk, PinAction_RePin, &pagePurged) ){
        purgeableCacheLeaveMutex();
        return NULL; /* something went very wrong */
      }
      if( pagePurged ){
        pPage=NULL;
      }
    }
  }
  if( pPage || createFlag==0 ){
    purgeableCachePinPage(pPage);
#if (TRACE_PURGEABLE_PCACHE>2)
    if( pPage ){
      fprintf(stderr, "[PCACHE] FETCH_    0x%x found iKey=%d page=0x%x nPage=%d iChunk=%d\n", 
            SQLITE_PTR_TO_INT(pCache), iKey, SQLITE_PTR_TO_INT(pPage), pCache->nPage, pPage->iChunk);
    } else {
      fprintf(stderr, "[PCACHE] FETCH_    0x%x missing iKey=%d page=NULL nPage=%d iChunk=-1\n", SQLITE_PTR_TO_INT(pCache), iKey, pCache->nPage);
    }
#endif
    goto fetch_out;
  }
  
  /* Step 3 of header comment. */
  nPinned = pCache->nPage - pCache->nRecyclable;
  assert( nPinned>=0 );
  assert( pCache->n90pct == pCache->nMax*9/10 );
  if( createFlag==1 && (nPinned>=pCache->n90pct) ){
#if (TRACE_PURGEABLE_PCACHE>2)
    fprintf(stderr, "[PCACHE] FETCH_    0x%x 90%% full iKey=%d page=NULL nPage=%d >= n90pct=%d\n", SQLITE_PTR_TO_INT(pCache), iKey, pCache->nPage, nPinned, pCache->n90pct);
#endif
    goto fetch_out;
  }
  
  if( pCache->nPage>=pCache->nHash && purgeableCacheResizeHash(pCache) ){
#if (TRACE_PURGEABLE_PCACHE>2)
    fprintf(stderr, "[PCACHE] FETCH_    0x%x full iKey=%d page=NULL (nPage=%d >= nHash=%d)\n", SQLITE_PTR_TO_INT(pCache), iKey, pCache->nPage, pCache->nHash);
#endif
    goto fetch_out;
  }
  
  /* Step 4. Recycle a page buffer if appropriate. */
  if( bPurgeable && pCache->pLruTail && ((pCache->nPage+1)>=pCache->nMax)){
    pPage = pCache->pLruTail;
    purgeableCacheRemoveFromHash(pPage);
    purgeableCachePinPage(pPage); /* prevents freeing the pPage if page data was purged */
    if( purgeableCacheRetainChunk(pPage->pCache, pPage->iChunk, PinAction_Recycle, &pagePurged) ){
      purgeableCacheFreePage(pPage);
      purgeableCacheLeaveMutex();
      return NULL; /* something went very wrong */
    }
    if( pagePurged ){
      ((PgHdr1 **)pPage->pPageData)[0] = pPage;
    }
    /* page & data contents will be cleared below */
  }
  
  /* Step 5. If a usable page buffer has still not been found, 
   ** attempt to allocate a new one. 
   */
  if( !pPage ){
    pPage = purgeableCacheAllocPage(pCache);
#if (TRACE_PURGEABLE_PCACHE>2)
    fprintf(stderr, "[PCACHE] FETCH_    0x%x alloc iKey=%d page=0x%x nPage=%d iChunk=%d\n", 
            SQLITE_PTR_TO_INT(pCache), iKey, SQLITE_PTR_TO_INT(pPage), pCache->nPage+1, (pPage!=NULL?(pPage->iChunk):(-1)));
  }else{
    fprintf(stderr, "[PCACHE] FETCH_    0x%x recycle iKey=%d (was %d) page=0x%x nPage=%d iChunk=%d\n", 
            SQLITE_PTR_TO_INT(pCache), iKey, pPage->iKey, SQLITE_PTR_TO_INT(pPage), pCache->nPage+1, (pPage!=NULL?(pPage->iChunk):(-1)));
#endif
  }    
  
  if( pPage ){
    /* reset the contents of the page and clear the data it points to but
    ** the linkage between the page header and the page data */
    unsigned int h = iKey % pCache->nHash;
    
    pCache->nPage++;
    pPage->iKey = iKey;
    pPage->pNext = pCache->apHash[h];
    pPage->pCache = pCache;
    pPage->pLruPrev = 0;
    pPage->pLruNext = 0;    
    if( bPurgeable ){
      *(void **)(PGHDR1_TO_PAGE(pPage)) = 0;
    }else{
      *(void **)(NP_PGHDR1_TO_PAGE(pPage)) = 0;
    }
    pCache->apHash[h] = pPage;
  }
  
fetch_out:
  if( pPage && iKey>pCache->iMaxKey ){
    pCache->iMaxKey = iKey;
  }
  if( createFlag==1 ) sqlite3EndBenignMalloc();
  purgeableCacheLeaveMutex();
  if( pagePurged && createFlag==0 ){
    return NULL;
  }
  if( bPurgeable ){
    return (pPage ? PGHDR1_TO_PAGE(pPage) : 0);
  }else{
    return (pPage ? NP_PGHDR1_TO_PAGE(pPage) : 0);
  }
}


/*
 ** Implementation of the sqlite3_pcache.xUnpin method.
 **
 ** Mark a page as unpinned (eligible for asynchronous recycling).
 */
static void purgeableCacheUnpin(sqlite3_pcache *p, void *pPg, int reuseUnlikely){
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  PgHdr1 *pPage = NULL;
  int bPurgeable = pCache->bPurgeable;

  purgeableCacheEnterMutex();
  if( bPurgeable ){
    pPage = PAGE_TO_PGHDR1(pPg);
  }else{
    pPage = NP_PAGE_TO_PGHDR1(pCache, pPg);
  }
  assert( pPage->pCache==pCache );
  
  /* It is an error to call this function if the page is already 
   ** part of the global LRU list.
   */
  assert( pPage->pLruPrev==0 && pPage->pLruNext==0 );
  assert( pCache->pLruHead!=pPage && pCache->pLruTail!=pPage );
  
#if (TRACE_PURGEABLE_PCACHE>1)
  fprintf(stderr, "[PCACHE] UNPIN     0x%x iKey=%d page=0x%x iChunk=%d reuse=%d\n", 
          SQLITE_PTR_TO_INT(pCache), pPage->iKey, SQLITE_PTR_TO_INT(pPage), pPage->iChunk, reuseUnlikely);
#endif
  if( reuseUnlikely ){
    if( bPurgeable ){
      purgeableCacheReleaseChunk(pCache, pPage->iChunk, PinAction_Clear);
    }
    purgeableCacheRemoveFromHash(pPage);
    purgeableCacheFreePage(pPage);
  }else{
    /* Add the page to the global LRU list. Normally, the page is added to
     ** the head of the list (last page to be recycled). However, if the 
     ** reuseUnlikely flag passed to this function is true, the page is added
     ** to the tail of the list (first page to be recycled).
     */
    if( bPurgeable ){
      purgeableCacheReleaseChunk(pCache, pPage->iChunk, PinAction_Unpin);
    }
    if( pCache->pLruHead ){
      pCache->pLruHead->pLruPrev = pPage;
      pPage->pLruNext = pCache->pLruHead;
      pCache->pLruHead = pPage;
    }else{
      pCache->pLruTail = pPage;
      pCache->pLruHead = pPage;
    }
    pCache->nRecyclable++;
#ifdef SQLITE_TEST
    purgeableCache.nRecyclable++;
#endif
  }
  purgeableCacheLeaveMutex();
}

/*
 ** Implementation of the sqlite3_pcache.xRekey method. 
 **
 */
static void purgeableCacheRekey(
  sqlite3_pcache *p,
  void *pPg,
  unsigned int iOld,
  unsigned int iNew
){
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  PgHdr1 *pPage = NULL;
  PgHdr1 **pp;
  unsigned int h; 
  
  purgeableCacheEnterMutex();
  if( pCache->bPurgeable ){
    pPage = PAGE_TO_PGHDR1(pPg);
  }else{
    pPage = NP_PAGE_TO_PGHDR1(pCache, pPg);
  }
  
  assert( pPage->iKey==iOld );
  assert( pPage->pCache==pCache );  
  
#if (TRACE_PURGEABLE_PCACHE>1)
  fprintf(stderr, "[PCACHE] REKEY     0x%x iKey=%d page=0x%x newKey=%d\n", 
          SQLITE_PTR_TO_INT(pCache), iOld, SQLITE_PTR_TO_INT(pPage), iNew);
#endif
  h = iOld%pCache->nHash;
  pp = &pCache->apHash[h];
  while( (*pp)!=pPage ){
    pp = &(*pp)->pNext;
  }
  *pp = pPage->pNext;
  
  h = iNew%pCache->nHash;
  pPage->iKey = iNew;
  pPage->pNext = pCache->apHash[h];
  pCache->apHash[h] = pPage;
  
  if( iNew>pCache->iMaxKey ){
    pCache->iMaxKey = iNew;
  }
  
  purgeableCacheLeaveMutex();
}

/*
 ** Implementation of the sqlite3_pcache.xTruncate method. 
 **
 ** Discard all unpinned pages in the cache with a page number equal to
 ** or greater than parameter iLimit. Any pinned pages with a page number
 ** equal to or greater than iLimit are implicitly unpinned.
 */
static void purgeableCacheTruncate(sqlite3_pcache *p, unsigned int iLimit){
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  purgeableCacheEnterMutex();
#if (TRACE_PURGEABLE_PCACHE>0)
  fprintf(stderr, "[PCACHE] TRUNCATE  0x%x iMaxKey=%d iLimit=%d\n", 
          SQLITE_PTR_TO_INT(pCache), pCache->iMaxKey, iLimit);
#endif
  if( iLimit<=pCache->iMaxKey ){
    purgeableCacheTruncateUnsafe(pCache, iLimit);
    pCache->iMaxKey = iLimit-1;
  }
  purgeableCacheLeaveMutex();
}

/*
 ** Implementation of the sqlite3_pcache.xDestroy method. 
 **
 ** Destroy a cache allocated using purgeableCacheCreate().
 */
static void purgeableCacheDestroy(sqlite3_pcache *p){
  PurgeablePCache *pCache = (PurgeablePCache *)p;
  int i, nChunks = 0;
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  PPCacheHdr *pCacheHdr = NULL;
#endif
  
  purgeableCacheEnterMutex();
#if (TRACE_PURGEABLE_PCACHE>0)
  fprintf(stderr, "[PCACHE] DESTROY   0x%x nMax=%d nChunks=%d\n", 
          SQLITE_PTR_TO_INT(pCache), pCache->nMax, nChunks);
#endif

#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  /* take this pCache out of the global linked list of pCaches */
  pCacheHdr=purgeableCache.pCacheHead;
  if( pCacheHdr&&pCacheHdr->pCache==pCache){
    purgeableCache.pCacheHead = pCacheHdr->pNext;
    pCacheHdr->pNext = NULL;
  }else{
    PPCacheHdr *pPrevHdr=NULL;
    while( pCacheHdr&&(pCacheHdr->pCache!=pCache) ){
      pPrevHdr = pCacheHdr;
      pCacheHdr = pCacheHdr->pNext;
    }
    if( pCacheHdr!=NULL ){
      assert(pPrevHdr!=NULL);
      pPrevHdr->pNext = pCacheHdr->pNext;
      pCacheHdr->pNext = NULL;
    }
  }
#endif
  
  purgeableCacheTruncateUnsafe(pCache, 0);
  nChunks = pCache->nChunks;
  for( i = 0; i < nChunks; i++ ){ 
    vm_address_t vm_addr = (vm_address_t)pCache->apChunks[i];
    kern_return_t err = vm_deallocate(mach_task_self(), vm_addr, PURGEABLE_CHUNK_SIZE);
#if (TRACE_PURGEABLE_PCACHE > 0)
    fprintf(stderr, "[PCACHE] .DESTROY  0x%x vm_deallocate chunk %d addr 0x%x\n", SQLITE_PTR_TO_INT(pCache), i, SQLITE_PTR_TO_INT(vm_addr));
#endif        
    if( err ){
      fprintf(stderr, "error returned from vm_deallocate: %d\n", err);
    }
  }
#ifdef SQLITE_TEST
  purgeableCache.nMaxPage -= pCache->nMax;
  purgeableCache.nMinPage -= pCache->nMin; /* unit tests */
#endif
  purgeableCacheLeaveMutex();
  sqlite3_free(pCache->apHash);
  sqlite3_free(pCache->apChunks);
  sqlite3_free(pCache->pChunkPinCount);
  sqlite3_free(pCache->pChunkUnpinCount);  
  sqlite3_free(pCache);
#if (PURGEABLE_CHUNK_QUEUE_SIZE>0)
  sqlite3_free(pCacheHdr);
#endif
}

/*
 ** This function is called during initialization (sqlite3_initialize()) to
 ** install the default pluggable cache module, assuming the user has not
 ** already provided an alternative.
 */
void sqlite3PCacheSetDefault(void){
  static sqlite3_pcache_methods defaultMethods = {
    0,                       /* pArg */
    purgeableCacheInit,             /* xInit */
    purgeableCacheShutdown,         /* xShutdown */
    purgeableCacheCreate,           /* xCreate */
    purgeableCacheCachesize,        /* xCachesize */
    purgeableCachePagecount,        /* xPagecount */
    purgeableCacheFetch,            /* xFetch */
    purgeableCacheUnpin,            /* xUnpin */
    purgeableCacheRekey,            /* xRekey */
    purgeableCacheTruncate,         /* xTruncate */
    purgeableCacheDestroy           /* xDestroy */
  };
  sqlite3_config(SQLITE_CONFIG_PCACHE, &defaultMethods);
}

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
 ** This function is called to free superfluous dynamically allocated memory
 ** held by the pager system. Memory in use by any SQLite pager allocated
 ** by the current thread may be sqlite3_free()ed.
 **
 ** nReq is the number of bytes of memory required. Once this much has
 ** been released, the function returns. The return value is the total number 
 ** of bytes of memory released.
 */
int sqlite3PcacheReleaseMemory(int nReq){
  int nFree = 0;
  if( purgeableCache.pStart==0 ){
    PgHdr1 *p;
    purgeableCacheEnterMutex();
    while( (nReq<0 || nFree<nReq) && (p=purgeableCache.pLruTail) ){
      if (p->pCache->bPurgeable) {
        nFree += sqlite3MallocSize(p);
      } else {
        nFree += sqlite3MallocSize(NP_PGHDR1_TO_PAGE(p));
      }
      purgeableCachePinPage(p);
      purgeableCacheRemoveFromHash(p);
      purgeableCacheFreePage(p);
    }
    purgeableCacheLeaveMutex();
  }
  return nFree;
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

#ifdef SQLITE_TEST
/*
 ** This function is used by test procedures to inspect the internal state
 ** of the global cache.
 */
void sqlite3PcacheStats(
  int *pnCurrent,      /* OUT: Total number of pages cached */
  int *pnMax,          /* OUT: Global maximum cache size */
  int *pnMin,          /* OUT: Sum of PCache1.nMin for purgeable caches */
  int *pnRecyclable    /* OUT: Total number of pages available for recycling */
){
  *pnCurrent = purgeableCache.nCurrentPage;
  *pnMax = purgeableCache.nMaxPage;
  *pnMin = purgeableCache.nMinPage;
  *pnRecyclable = purgeableCache.nRecyclable;
}
#endif

#else

/* Need to declare this SPI */
#if (SQLITE_ENABLE_APPLE_SPI>0)
void _sqlite3_purgeEligiblePagerCacheMemory(void) {}
#endif

#endif /* SQLITE_OMIT_PURGEABLE_PCACHE */
