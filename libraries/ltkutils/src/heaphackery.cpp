// heaphackery.cpp
// 
// Copyright (c) 2010 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//
// Contributors:
// Adrian Issott (Nokia) - Updates for kernel-side alloc helper & RHybridHeap v2
//

#include "heaputils.h"
#include "heapoffsets.h"

enum THeapUtilsPanic
	{
	EUnsupportedAllocatorType,
	EUserHeapOffsetRequestedForKernelHeap,
	};

#ifdef __KERNEL_MODE__

#include <kern_priv.h>
#define MEM Kern
__ASSERT_COMPILE(sizeof(LtkUtils::RUserAllocatorHelper) == 10*4);
#define KERN_ENTER_CS() NKern::ThreadEnterCS()
#define KERN_LEAVE_CS() NKern::ThreadLeaveCS()
#ifdef _DEBUG
#define LOG(args...) Kern::Printf(args)
#else
#define LOG(args...)
#endif
#define HUEXPORT_C
#define PANIC(r) Kern::Fault( "HeapUtils", (r));

#else // __KERNEL_MODE__

#include <e32std.h>
#define MEM User
#define KERN_ENTER_CS()
#define KERN_LEAVE_CS()
#ifdef _DEBUG
#include <e32debug.h>
#define LOG(args...) RDebug::Printf(args)
#else
#define LOG(args...)
#endif
#ifdef STANDALONE_ALLOCHELPER
#define HUEXPORT_C
#else
#define HUEXPORT_C EXPORT_C
#endif
#define PANIC(r) User::Panic( _L("HeapUtils"), (r));

#endif // __KERNEL_MODE__

#define RETURN_IF_ERR(x) do { TInt _err = (x); if (_err) { LOG("Returning err %d from %d", _err, __LINE__); return _err; } } while (0)

using LtkUtils::RAllocatorHelper;

#ifndef TEST_HYBRIDHEAP_V2_ASSERTS
const TUint KPageSize = 4096;
#endif // TEST_HYBRIDHEAP_V2_ASSERTS

__ASSERT_COMPILE(sizeof(RAllocatorHelper) == 9*4);

// RAllocatorHelper

HUEXPORT_C RAllocatorHelper::RAllocatorHelper()
	: iAllocatorAddress(0), iAllocatorType(EAllocatorNotSet), iInfo(NULL)
	, iIsKernelHeapAllocator(EFalse), iTempSlabBitmap(NULL), iPageCache(NULL), iPageCacheAddr(0)
#ifdef __KERNEL_MODE__
	, iChunk(NULL)
#endif
	{
	}

namespace LtkUtils
	{
	class THeapInfo
		{
	public:
		THeapInfo()
			{
			ClearStats();
			}

		void ClearStats()
			{
			memclr(this, sizeof(THeapInfo));
			}
		
		TUint iValidInfo;		
		TInt iAllocatedSize; // number of bytes in allocated cells (excludes free cells, cell header overhead)
		TInt iCommittedSize; // amount of memory actually committed (includes cell header overhead, gaps smaller than an MMU page)
		TInt iAllocationCount; // number of allocations currently
		TInt iMaxCommittedSize; // or thereabouts
		TInt iMinCommittedSize;
		TInt iUnusedPages;
		TInt iCommittedFreeSpace;
		// Heap-only stats
		TInt iHeapFreeCellCount;
		// Hybrid-only stats
		TInt iDlaAllocsSize;
		TInt iDlaAllocsCount;
		TInt iDlaFreeSize;
		TInt iDlaFreeCount;
		TInt iSlabAllocsSize;
		TInt iSlabAllocsCount;
		TInt iPageAllocsSize;
		TInt iPageAllocsCount;
		TInt iSlabFreeCellSize;
		TInt iSlabFreeCellCount;
		TInt iSlabFreeSlabSize;
		TInt iSlabFreeSlabCount;
		};
	}

const TInt KTempBitmapSize = 256; // KMaxSlabPayload / mincellsize, technically. Close enough.

#ifdef __KERNEL_MODE__

TLinAddr LtkUtils::RAllocatorHelper::GetKernelAllocator(DChunk* aKernelChunk) 
	{
	TLinAddr allocatorAddress;
#ifdef __WINS__
	allocatorAddress = (TLinAddr)aKernelChunk->Base();
#else
	// Copied from P::KernelInfo
	const TRomHeader& romHdr=Epoc::RomHeader();
	const TRomEntry* primaryEntry=(const TRomEntry*)Kern::SuperPage().iPrimaryEntry;
	const TRomImageHeader* primaryImageHeader=(const TRomImageHeader*)primaryEntry->iAddressLin;
	TLinAddr stack = romHdr.iKernDataAddress + Kern::RoundToPageSize(romHdr.iTotalSvDataSize);
	TLinAddr heap = stack + Kern::RoundToPageSize(primaryImageHeader->iStackSize);
	allocatorAddress = heap;
#endif
	return allocatorAddress;
	}

TInt RAllocatorHelper::OpenKernelHeap()
	{
	SetIsKernelHeapAllocator(ETrue);
	
	_LIT(KName, "SvHeap");
	NKern::ThreadEnterCS();
	DObjectCon* chunkContainer = Kern::Containers()[EChunk];
	chunkContainer->Wait();
	const TInt chunkCount = chunkContainer->Count();
	DChunk* foundChunk = NULL;
	for(TInt i=0; i<chunkCount; i++)
		{
		DChunk* chunk = (DChunk*)(*chunkContainer)[i];
		if (chunk->NameBuf() && chunk->NameBuf()->Find(KName) != KErrNotFound)
			{
			// Found it. No need to open it, we can be fairly confident the kernel heap isn't going to disappear from under us
			foundChunk = chunk;
			break;
			}
		}
	iChunk = foundChunk;
	chunkContainer->Signal();

	iAllocatorAddress = GetKernelAllocator(foundChunk);

	// It looks like DChunk::iBase/DChunk::iFixedBase should both be ok for the kernel chunk
	// aChunkMaxSize is only used for trying the middle of the chunk for hybrid allocatorness, and the kernel heap doesn't use that (thankfully). So we can safely pass in zero.
	TInt err = OpenChunkHeap((TLinAddr)foundChunk->Base(), 0); 

	if (!err) err = FinishConstruction();
	NKern::ThreadLeaveCS();
	return err;
	}

#else

HUEXPORT_C TInt RAllocatorHelper::Open(RAllocator* aAllocator)
	{
	iAllocatorAddress = (TLinAddr)aAllocator;
	TInt udeb = EuserIsUdeb();
	if (udeb < 0) return udeb; // error

	TInt err = IdentifyAllocatorType(udeb);
	if (!err)
		{
		err = FinishConstruction(); // Allocate everything up front
		}
	if (!err)
		{
		// We always stealth our own allocations, again to avoid tripping up allocator checks
		SetCellNestingLevel(iInfo, -1);
		SetCellNestingLevel(iTempSlabBitmap, -1);
		SetCellNestingLevel(iPageCache, -1);
		}
	return err;
	}

#endif

TInt RAllocatorHelper::FinishConstruction()
	{
	TInt err = KErrNone;
	KERN_ENTER_CS();
	if (!iInfo)
		{
		iInfo = new THeapInfo;
		if (!iInfo) err = KErrNoMemory;
		}
	if (!err && !iTempSlabBitmap)
		{
		iTempSlabBitmap = (TUint8*)MEM::Alloc(KTempBitmapSize);
		if (!iTempSlabBitmap) err = KErrNoMemory;
		}
	if (!err && !iPageCache)
		{
		iPageCache = MEM::Alloc(KPageSize);
		if (!iPageCache) err = KErrNoMemory;
		}

	if (err)
		{
		delete iInfo;
		iInfo = NULL;
		MEM::Free(iTempSlabBitmap);
		iTempSlabBitmap = NULL;
		MEM::Free(iPageCache);
		iPageCache = NULL;
		}
	KERN_LEAVE_CS();
	return err;
	}

TInt RAllocatorHelper::ReadWord(TLinAddr aLocation, TUint32& aResult) const
	{
	// Check if we can satisfy the read from the cache
	if (aLocation >= iPageCacheAddr)
		{
		TUint offset = aLocation - iPageCacheAddr;
		if (offset < KPageSize)
			{
			aResult = ((TUint32*)iPageCache)[offset >> 2];
			return KErrNone;
			}
		}

	// If we reach here, not in page cache. Try and read in the new page
	if (iPageCache)
		{
		TLinAddr pageAddr = aLocation & ~(KPageSize-1);
		TInt err = ReadData(pageAddr, iPageCache, KPageSize);
		if (!err)
			{
			iPageCacheAddr = pageAddr;
			aResult = ((TUint32*)iPageCache)[(aLocation - iPageCacheAddr) >> 2];
			return KErrNone;
			}
		}

	// All else fails, try just reading it uncached
	return ReadData(aLocation, &aResult, sizeof(TUint32));
	}

TInt RAllocatorHelper::ReadByte(TLinAddr aLocation, TUint8& aResult) const
	{
	// Like ReadWord but 8-bit

	// Check if we can satisfy the read from the cache
	if (aLocation >= iPageCacheAddr)
		{
		TUint offset = aLocation - iPageCacheAddr;
		if (offset < KPageSize)
			{
			aResult = ((TUint8*)iPageCache)[offset];
			return KErrNone;
			}
		}

	// If we reach here, not in page cache. Try and read in the new page
	if (iPageCache)
		{
		TLinAddr pageAddr = aLocation & ~(KPageSize-1);
		TInt err = ReadData(pageAddr, iPageCache, KPageSize);
		if (!err)
			{
			iPageCacheAddr = pageAddr;
			aResult = ((TUint8*)iPageCache)[(aLocation - iPageCacheAddr)];
			return KErrNone;
			}
		}

	// All else fails, try just reading it uncached
	return ReadData(aLocation, &aResult, sizeof(TUint8));
	}


TInt RAllocatorHelper::WriteWord(TLinAddr aLocation, TUint32 aWord)
	{
	// Invalidate the page cache if necessary
	if (aLocation >= iPageCacheAddr && aLocation - iPageCacheAddr < KPageSize)
		{
		iPageCacheAddr = 0;
		}

	return WriteData(aLocation, &aWord, sizeof(TUint32));
	}

TInt RAllocatorHelper::ReadData(TLinAddr aLocation, TAny* aResult, TInt aSize) const
	{
	// RAllocatorHelper base class impl is for allocators in same address space, so just copy it
	memcpy(aResult, (const TAny*)aLocation, aSize);
	return KErrNone;
	}

TInt RAllocatorHelper::WriteData(TLinAddr aLocation, const TAny* aData, TInt aSize)
	{
	memcpy((TAny*)aLocation, aData, aSize);
	return KErrNone;
	}

#ifdef __KERNEL_MODE__

LtkUtils::RUserAllocatorHelper::RUserAllocatorHelper()
	: iThread(NULL)
	{}

void LtkUtils::RUserAllocatorHelper::Close()
	{
	NKern::ThreadEnterCS();
	if (iThread)
		{
		iThread->Close(NULL);
		}
	iThread = NULL;
	RAllocatorHelper::Close();
	NKern::ThreadLeaveCS();
	}

TInt LtkUtils::RUserAllocatorHelper::ReadData(TLinAddr aLocation, TAny* aResult, TInt aSize) const
	{
	return Kern::ThreadRawRead(iThread, (const TAny*)aLocation, aResult, aSize);
	}

TInt LtkUtils::RUserAllocatorHelper::WriteData(TLinAddr aLocation, const TAny* aData, TInt aSize)
	{
	return Kern::ThreadRawWrite(iThread, (TAny*)aLocation, aData, aSize);
	}

TInt LtkUtils::RUserAllocatorHelper::TryLock()
	{
	return KErrNotSupported;
	}

void LtkUtils::RUserAllocatorHelper::TryUnlock()
	{
	// Not supported
	}

TInt LtkUtils::RUserAllocatorHelper::OpenUserHeap(TUint aThreadId, TLinAddr aAllocatorAddress, TBool aEuserIsUdeb)
	{
	NKern::ThreadEnterCS();
	DObjectCon* threads = Kern::Containers()[EThread];
	threads->Wait();
	iThread = Kern::ThreadFromId(aThreadId);
	if (iThread && iThread->Open() != KErrNone)
		{
		// Failed to open
		iThread = NULL;
		}
	threads->Signal();
	NKern::ThreadLeaveCS();
	if (!iThread) return KErrNotFound;
	iAllocatorAddress = aAllocatorAddress;
	TInt err = IdentifyAllocatorType(aEuserIsUdeb);
	if (err) Close();
	return err;
	}

LtkUtils::RKernelCopyAllocatorHelper::RKernelCopyAllocatorHelper()
	: iCopiedChunk(NULL), iOffset(0)
	{
	SetIsKernelHeapAllocator(ETrue);
	}

TInt LtkUtils::RKernelCopyAllocatorHelper::OpenCopiedHeap(DChunk* aOriginalChunk, DChunk* aCopiedChunk, TInt aOffset)
	{
	TInt err = aCopiedChunk->Open();
	if (!err)
		{
		iCopiedChunk = aCopiedChunk;
		iOffset = aOffset;

		// We need to set iAllocatorAddress to point to the allocator in the original chunk and not the copy
		// because all the internal pointers will be relative to that. Instead we use iOffset in the Read / Write Data 
		// calls
		iAllocatorAddress = GetKernelAllocator(aOriginalChunk);
		
		// It looks like DChunk::iBase/DChunk::iFixedBase should both be ok for the kernel chunk
		// aChunkMaxSize is only used for trying the middle of the chunk for hybrid allocatorness, and the kernel heap doesn't use that (thankfully). So we can safely pass in zero.
		err = OpenChunkHeap((TLinAddr)aCopiedChunk->Base(), 0);
		}
	
	return err;
	}

DChunk* LtkUtils::RKernelCopyAllocatorHelper::OpenUnderlyingChunk()
	{
	// We should never get here
	__NK_ASSERT_ALWAYS(EFalse);
	return NULL;
	}

void LtkUtils::RKernelCopyAllocatorHelper::Close()
	{
	if (iCopiedChunk)
		{
		NKern::ThreadEnterCS();
		iCopiedChunk->Close(NULL);
		iCopiedChunk = NULL;
		NKern::ThreadLeaveCS();
		}
	iOffset = 0;
	RAllocatorHelper::Close();
	}

TInt LtkUtils::RKernelCopyAllocatorHelper::ReadData(TLinAddr aLocation, TAny* aResult, TInt aSize) const
	{
	memcpy(aResult, (const TAny*)(aLocation+iOffset), aSize);
	return KErrNone;
	}

TInt LtkUtils::RKernelCopyAllocatorHelper::WriteData(TLinAddr aLocation, const TAny* aData, TInt aSize)
	{
	memcpy((TAny*)(aLocation+iOffset), aData, aSize);
	return KErrNone;
	}

TInt LtkUtils::RKernelCopyAllocatorHelper::TryLock()
	{
	return KErrNotSupported;
	}

void LtkUtils::RKernelCopyAllocatorHelper::TryUnlock()
	{
	// Not supported
	}

#endif // __KERNEL_MODE__

TInt RAllocatorHelper::OpenChunkHeap(TLinAddr aChunkBase, TInt aChunkMaxSize)
	{
	LOG("RAllocatorHelper::OpenChunkHeap(0x%08x, 0x%08x)", aChunkBase, aChunkMaxSize);
#ifdef __KERNEL_MODE__
	// Must be in CS
	// Assumes that this only ever gets called for the kernel heap. Otherwise goes through RKernelSideAllocatorHelper::OpenUserHeap.
	TInt udeb = EFalse; // We can't figure this out until after we've got the heap
	TBool isTheKernelHeap = ETrue;
#else
	// Assumes the chunk isn't the kernel heap. It's not a good idea to try messing with the kernel heap from user side...
	TInt udeb = EuserIsUdeb();
	if (udeb < 0) return udeb; // error
	TBool isTheKernelHeap = EFalse;
#endif

	if (iAllocatorAddress == 0)
		{
		// Subclasses with more knowledge about the layout of the allocator within the chunk may have already set the iAllocatorAddress (eg kernel heap's allocator doesn't start at the chunk base)
		iAllocatorAddress = aChunkBase;
		}

	TInt err = IdentifyAllocatorType(udeb, isTheKernelHeap);
	if (err == KErrNone && iAllocatorType == EAllocatorUnknown)
		{
		// We've no reason to assume it's an allocator because we don't know the iAllocatorAddress actually is an RAllocator*
		err = KErrNotFound;
		}
	if (err && aChunkMaxSize > 0 && iAllocatorAddress == aChunkBase)
		{
		TInt oldErr = err;
		TAllocatorType oldType = iAllocatorType;
		// Try middle of chunk, in case it's an RHybridHeap
		iAllocatorAddress += aChunkMaxSize / 2;
		LOG("RAllocatorHelper::OpenChunkHeap(0x%08x, 0x%08x) retrying IdentifyAllocatorType at 0x%08x", aChunkBase, aChunkMaxSize, iAllocatorAddress);
		err = IdentifyAllocatorType(udeb, isTheKernelHeap);
		if (err || iAllocatorType == EAllocatorUnknown)
			{
			// No better than before
			iAllocatorAddress = aChunkBase;
			iAllocatorType = oldType;
			err = oldErr;
			}
		}
#ifdef __KERNEL_MODE__
	if (err == KErrNone)
		{
		// Now we know the allocator, we can figure out the udeb-ness
		RAllocator* kernelAllocator = reinterpret_cast<RAllocator*>(iAllocatorAddress);
		kernelAllocator->DebugFunction(RAllocator::ESetFail, (TAny*)9999, (TAny*)0); // Use an invalid fail reason - this should have no effect on the operation of the heap
		TInt err = kernelAllocator->DebugFunction(7, NULL, NULL); // 7 is RAllocator::TAllocDebugOp::EGetFail
		if (err == 9999)
			{
			// udeb hybrid heap (v1 or v2)
			udeb = ETrue;
			}
		else if (err == KErrNotSupported)
			{
			// Old heap - fall back to slightly nasty non-thread-safe method
			kernelAllocator->DebugFunction(RAllocator::ESetFail, (TAny*)RAllocator::EFailNext, (TAny*)1);
			TAny* res = Kern::Alloc(4);
			if (!res) udeb = ETrue;
			Kern::Free(res);
			}
		else
			{
			// it's new urel
			}

		// Put everything back
		kernelAllocator->DebugFunction(RAllocator::ESetFail, (TAny*)RAllocator::ENone, (TAny*)0);
		// And update the type now we know the udeb-ness for certain
		err = IdentifyAllocatorType(udeb, isTheKernelHeap);
		}
#endif
	return err;
	}


// The guts of RAllocatorHelper

enum TWhatToGet
	{
	ECommitted = 1,
	EAllocated = 2,
	ECount = 4,
	EMaxSize = 8,
	EUnusedPages = 16,
	ECommittedFreeSpace = 32,
	EMinSize = 64,
	EHybridStats = 128,
	};

TInt RAllocatorHelper::TryLock()
	{
#ifdef __KERNEL_MODE__
	NKern::ThreadEnterCS();
	DMutex* m = *(DMutex**)(iAllocatorAddress + _FOFF(RHackHeap, iLock));
	if (m) Kern::MutexWait(*m);
	return KErrNone;
#else
	if (iAllocatorType != EAllocatorNotSet && iAllocatorType != EAllocatorUnknown)
		{
		RFastLock& lock = *reinterpret_cast<RFastLock*>(iAllocatorAddress + _FOFF(RHackHeap, iLock));
		lock.Wait();
		return KErrNone;
		}
	return KErrNotSupported;
#endif
	}

void RAllocatorHelper::TryUnlock()
	{
#ifdef __KERNEL_MODE__
	DMutex* m = *(DMutex**)(iAllocatorAddress + _FOFF(RHackHeap, iLock));
	if (m) Kern::MutexSignal(*m);
	NKern::ThreadLeaveCS();
#else
	if (iAllocatorType != EAllocatorNotSet && iAllocatorType != EAllocatorUnknown)
		{
		RFastLock& lock = *reinterpret_cast<RFastLock*>(iAllocatorAddress + _FOFF(RHackHeap, iLock));
		lock.Signal();
		}
#endif
	}

HUEXPORT_C void RAllocatorHelper::Close()
	{
	KERN_ENTER_CS();
	iAllocatorType = EAllocatorNotSet;
	iAllocatorAddress = 0;
	delete iInfo;
	iInfo = NULL;
	MEM::Free(iTempSlabBitmap);
	iTempSlabBitmap = NULL;
	MEM::Free(iPageCache);
	iPageCache = NULL;
	iPageCacheAddr = 0;
	SetIsKernelHeapAllocator(EFalse);
	KERN_LEAVE_CS();
	}

TInt RAllocatorHelper::IdentifyAllocatorType(TBool aAllocatorIsUdeb, TBool aIsTheKernelHeap)
	{
	iAllocatorType = EAllocatorNotSet;
	SetIsKernelHeapAllocator(aIsTheKernelHeap);

	TUint32 handlesPtr = 0;
	TInt err = ReadWord(iAllocatorAddress + _FOFF(RHackAllocator, iHandles), handlesPtr);

	RETURN_IF_ERR(err);
	if (aIsTheKernelHeap || 
		handlesPtr == iAllocatorAddress + _FOFF(RHackHeap, iChunkHandle) || 
		handlesPtr == iAllocatorAddress + _FOFF(RHackHeap, iLock))
		{
		// It's an RHeap of some kind - I doubt any other RAllocator subclass will use iHandles in this way
		TUint32 base = 0;
		err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iBase), base);
		RETURN_IF_ERR(err);
		TInt objsize = (TInt)base - (TInt)iAllocatorAddress;
		LOG("RAllocatorHelper::IdentifyAllocatorType() - allocator size is: 0x%08x", objsize);

		if (objsize <= HeapV1::KUserInitialHeapMetaDataSize)
			{
			// Old RHeap
			iAllocatorType = aAllocatorIsUdeb ? EUdebOldRHeap : EUrelOldRHeap;
			}
		else if (objsize > HybridV2::KSelfReferenceOffset) // same value as HybridV1::KMallocStateOffset so will be true for RHybridHeap V1 and V2 
			{
			// First and second versions of hybrid heap are bigger than the original RHeap
			// But the user and kernel side versions have different sizes

			TUint32 possibleSelfRef = 0; // in RHybridHeap v2 ...
			err = ReadWord(iAllocatorAddress + HybridV2::KSelfReferenceOffset, possibleSelfRef);
			RETURN_IF_ERR(err);

			// Only the second version references itself
			if (possibleSelfRef == iAllocatorAddress)
				{
				iAllocatorType = aAllocatorIsUdeb ? EUdebHybridHeapV2 : EUrelHybridHeapV2;				
				}
			else if ( objsize < HybridQt::KUserInitialHeapMetaDataSize )
				{
				iAllocatorType = aAllocatorIsUdeb ? EUdebHybridHeap : EUrelHybridHeap;
				}
			else
				{
				iAllocatorType = aAllocatorIsUdeb ? EUdebHybridHeapQt : EUrelHybridHeapQt;
				}
			
			
			}
		else 
			{
			iAllocatorType = EAllocatorUnknown;
			}
		}
	else
		{
		iAllocatorType = EAllocatorUnknown;
		} 
	
	LOG("RAllocatorHelper::IdentifyAllocatorType() - allocator at 0x%08x has type: %d", iAllocatorAddress, iAllocatorType);
	
	return KErrNone;
	}

HUEXPORT_C TInt RAllocatorHelper::SetCellNestingLevel(TAny* aCell, TInt aNestingLevel)
	{
	TInt err = KErrNone;

	switch (iAllocatorType)
		{
		// All of them are in the same place amazingly
		case EUdebOldRHeap:
		case EUdebHybridHeap:
		case EUdebHybridHeapV2:
		case EUdebHybridHeapQt:
			{
			TLinAddr nestingAddr = (TLinAddr)aCell - 8;
			err = WriteWord(nestingAddr, aNestingLevel);
			break;
			}
		default:
			break;
		}
	return err;
	}

HUEXPORT_C TInt RAllocatorHelper::GetCellNestingLevel(TAny* aCell, TInt& aNestingLevel)
	{
	switch (iAllocatorType)
		{
		// All of them are in the same place amazingly		
		case EUdebOldRHeap:
		case EUdebHybridHeap:
		case EUdebHybridHeapV2:
		case EUdebHybridHeapQt:
			{
			TLinAddr nestingAddr = (TLinAddr)aCell - 8;
			return ReadWord(nestingAddr, (TUint32&)aNestingLevel);
			}
		default:
			break;
		}
	return 1;
	}

TInt RAllocatorHelper::RefreshDetails(TUint aMask)
	{
	TInt err = FinishConstruction();
	RETURN_IF_ERR(err);

	// Invalidate the page cache
	iPageCacheAddr = 0;

	TryLock();
	err = DoRefreshDetails(aMask);
	TryUnlock();
	return err;
	}

const TInt KHeapWalkStatsForOldHeap = (EUnusedPages|ECommittedFreeSpace);
const TInt KHeapWalkStatsForNewHeap = (EAllocated|ECount|EUnusedPages|ECommittedFreeSpace|EHybridStats);

TInt RAllocatorHelper::DoRefreshDetails(TUint aMask)
	{
	TInt err = KErrNone;
	switch (iAllocatorType)
		{
		case EUrelOldRHeap:
		case EUdebOldRHeap:
			{
			if (aMask & ECommitted)
				{
				// The old RHeap::Size() used to use iTop - iBase, which was effectively chunkSize - sizeof(RHeap)
				// I think that for CommittedSize we should include the size of the heap object, just as it includes
				// the size of heap cell metadata and overhead. Plus it makes sure the committedsize is a multiple of the page size
				TUint32 top = 0;
				//TUint32 base = 0;
				//err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iBase), base);
				//RETURN_IF_ERR(err);
				err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iTop), top);
				RETURN_IF_ERR(err);

				//iInfo->iCommittedSize = top - base;
				iInfo->iCommittedSize = top - iAllocatorAddress;
				iInfo->iValidInfo |= ECommitted;
				}
			if (aMask & EAllocated)
				{
				TUint32 allocSize = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackAllocator, iTotalAllocSize), allocSize);
				RETURN_IF_ERR(err);
				iInfo->iAllocatedSize = allocSize;
				iInfo->iValidInfo |= EAllocated;
				}
			if (aMask & ECount)
				{
				TUint32 count = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackAllocator, iCellCount), count);
				RETURN_IF_ERR(err);
				iInfo->iAllocationCount = count;
				iInfo->iValidInfo |= ECount;
				}
			if (aMask & EMaxSize)
				{
				TUint32 maxlen = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iMaxLength), maxlen);
				RETURN_IF_ERR(err);
				iInfo->iMaxCommittedSize = maxlen;
				iInfo->iValidInfo |= EMaxSize;
				}
			if (aMask & EMinSize)
				{
				TUint32 minlen = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iMaxLength) - 4, minlen); // This isn't a typo! iMinLength is 4 bytes before iMaxLength, on old heap ONLY
				RETURN_IF_ERR(err);
				iInfo->iMinCommittedSize = minlen;
				iInfo->iValidInfo |= EMinSize;
				}
			if (aMask & KHeapWalkStatsForOldHeap)
				{
				// Need a heap walk
				iInfo->ClearStats();
				iInfo->iValidInfo = 0;
				err = DoWalk(&WalkForStats, NULL);
				if (err == KErrNone) iInfo->iValidInfo |= KHeapWalkStatsForOldHeap;
				}
			return err;
			}
		case EUrelHybridHeap:
		case EUdebHybridHeap:
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			{
			TBool needWalk = EFalse;
			if (aMask & ECommitted)
				{
				// RAllocator::Size uses iChunkSize - sizeof(RHybridHeap);
				// We can't do exactly the same, because we can't calculate sizeof(RHybridHeap), only ROUND_UP(sizeof(RHybridHeap), iAlign)
				// And if fact we don't bother and just use iChunkSize
				TUint32 chunkSize = 0;
				err = ReadWord(iAllocatorAddress + KChunkSizeOffset, chunkSize);
				RETURN_IF_ERR(err);
				//TUint32 baseAddr = 0;
				//err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iBase), baseAddr);
				//RETURN_IF_ERR(err);
				iInfo->iCommittedSize = chunkSize; // - (baseAddr - iAllocatorAddress);
				iInfo->iValidInfo |= ECommitted;
				}
			if (aMask & (EAllocated|ECount))
				{
				if (iAllocatorType == EUdebHybridHeap)
					{
					// Easy, just get them from the counter
					TUint32 totalAlloc = 0;
					err = ReadWord(iAllocatorAddress + _FOFF(RHackAllocator, iTotalAllocSize), totalAlloc);
					RETURN_IF_ERR(err);
					iInfo->iAllocatedSize = totalAlloc;
					iInfo->iValidInfo |= EAllocated;

					TUint32 cellCount = 0;
					err = ReadWord(iAllocatorAddress + _FOFF(RHackAllocator, iCellCount), cellCount);
					RETURN_IF_ERR(err);
					iInfo->iAllocationCount = cellCount;
					iInfo->iValidInfo |= ECount;
					}
				else
					{
					// A heap walk is needed
					needWalk = ETrue;
					}
				}
			if (aMask & EMaxSize)
				{
				TUint32 maxlen = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iMaxLength), maxlen);
				RETURN_IF_ERR(err);
				iInfo->iMaxCommittedSize = maxlen;
				iInfo->iValidInfo |= EMaxSize;
				}
			if (aMask & EMinSize)
				{
				TUint32 minlen = 0;
				err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iAlign) + 4*4, minlen); // iMinLength is in different place to old RHeap
				RETURN_IF_ERR(err);
				iInfo->iMinCommittedSize = minlen;
				iInfo->iValidInfo |= EMinSize;
				}
			if (aMask & (EUnusedPages|ECommittedFreeSpace|EHybridStats))
				{
				// EAllocated and ECount have already been taken care of above
				needWalk = ETrue;
				}

			if (needWalk)
				{
				iInfo->ClearStats();
				iInfo->iValidInfo = 0;
				err = DoWalk(&WalkForStats, NULL);
				if (err == KErrNone) iInfo->iValidInfo |= KHeapWalkStatsForNewHeap;
				}
			return err;
			}
		default:
			return KErrNotSupported;
		}
	}

TInt RAllocatorHelper::CheckValid(TUint aMask)
	{
	if (iInfo && (iInfo->iValidInfo & aMask) == aMask)
		{
		return KErrNone;
		}
	else
		{
		return RefreshDetails(aMask);
		}
	}

HUEXPORT_C TInt RAllocatorHelper::CommittedSize()
	{
	TInt err = CheckValid(ECommitted);
	RETURN_IF_ERR(err);
	return iInfo->iCommittedSize;
	}

HUEXPORT_C TInt RAllocatorHelper::AllocatedSize()
	{
	TInt err = CheckValid(EAllocated);
	RETURN_IF_ERR(err);
	return iInfo->iAllocatedSize;
	}

HUEXPORT_C TInt RAllocatorHelper::AllocationCount()
	{
	TInt err = CheckValid(ECount);
	RETURN_IF_ERR(err);
	return iInfo->iAllocationCount;
	}

HUEXPORT_C TInt RAllocatorHelper::RefreshDetails()
	{
	if (iInfo) 
		{
		return RefreshDetails(iInfo->iValidInfo);
		}
	else 
		{
		return RefreshDetails(0);
		}
	}

HUEXPORT_C TInt RAllocatorHelper::MaxCommittedSize()
	{
	TInt err = CheckValid(EMaxSize);
	RETURN_IF_ERR(err);
	return iInfo->iMaxCommittedSize;
	}

HUEXPORT_C TInt RAllocatorHelper::MinCommittedSize()
	{
	TInt err = CheckValid(EMinSize);
	RETURN_IF_ERR(err);
	return iInfo->iMinCommittedSize;
	}

HUEXPORT_C TInt RAllocatorHelper::AllocCountForCell(TAny* aCell) const
	{
	TUint32 allocCount = 0;
	switch (iAllocatorType)
		{
		// All of them are in the same place amazingly
		case EUdebOldRHeap:
		case EUdebHybridHeap: 
		case EUdebHybridHeapV2:
		case EUdebHybridHeapQt:
			{
			TLinAddr allocCountAddr = (TLinAddr)aCell - 4;
			TInt err = ReadWord(allocCountAddr, allocCount);
			RETURN_IF_ERR(err);
			return (TInt)allocCount;
			}
		default:
			break;
		}
	return 1;
	}

//

void RAllocatorHelper::SetIsKernelHeapAllocator(TBool aIsKernelHeapAllocator)
	{
	iIsKernelHeapAllocator = aIsKernelHeapAllocator;
	}

TBool RAllocatorHelper::GetIsKernelHeapAllocator() const
	{
	return iIsKernelHeapAllocator;
	}

TInt RAllocatorHelper::PageMapOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}
	
	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserPageMapOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserPageMapOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserPageMapOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}
	}

TInt RAllocatorHelper::MemBaseOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}
	
	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserMemBaseOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserMemBaseOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserMemBaseOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}
	}

TInt RAllocatorHelper::MallocStateOffset() const
	{
	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KMallocStateOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
				return HybridQt::KMallocStateOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			if (GetIsKernelHeapAllocator())
				{
				return HybridV2::KKernelMallocStateOffset;
				}
			else 
				{
				return HybridV2::KUserMallocStateOffset;
				}
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

TInt RAllocatorHelper::SparePageOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}

	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserSparePageOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserSparePageOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserSparePageOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}
	}

TInt RAllocatorHelper::PartialPageOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}

	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserPartialPageOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserPartialPageOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserPartialPageOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

TInt RAllocatorHelper::FullSlabOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}

	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserFullSlabOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserFullSlabOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserFullSlabOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

TInt RAllocatorHelper::SlabAllocOffset() const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}

	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserSlabAllocOffset;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserSlabAllocOffset;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserSlabAllocOffset;
			
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

TInt RAllocatorHelper::SlabPadding( TInt aSize ) const
	{
	if (GetIsKernelHeapAllocator())
		{
		PANIC(EUserHeapOffsetRequestedForKernelHeap);
		}

	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return 0;
			
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridCom::KMaxSlabPayload % aSize;
			
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

TInt RAllocatorHelper::UserInitialHeapMetaDataSize() const
	{
	switch (iAllocatorType)
		{
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return HybridV1::KUserInitialHeapMetaDataSize;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return HybridV2::KUserInitialHeapMetaDataSize;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return HybridQt::KUserFullSlabOffset;
		default:
			PANIC(EUnsupportedAllocatorType);
			return KErrNotSupported; // only needed to make the compiler happy
		}	
	}

struct SContext3
	{
	RAllocatorHelper::TWalkFunc3 iOrigWalkFn;
	TAny* iOrigContext;
	};

TBool RAllocatorHelper::DispatchClientWalkCallback(RAllocatorHelper& aHelper, TAny* aContext, RAllocatorHelper::TExtendedCellType aCellType, TLinAddr aCellPtr, TInt aCellLength)
	{
	WalkForStats(aHelper, NULL, aCellType, aCellPtr, aCellLength);
	SContext3* context = static_cast<SContext3*>(aContext);
	return (*context->iOrigWalkFn)(aHelper, context->iOrigContext, aCellType, aCellPtr, aCellLength);
	}

HUEXPORT_C TInt RAllocatorHelper::Walk(TWalkFunc3 aCallbackFn, TAny* aContext)
	{
	// Might as well take the opportunity of updating our stats at the same time as walking the heap for the client
	SContext3 context = { aCallbackFn, aContext };

	TInt err = FinishConstruction(); // In case this hasn't been done yet
	RETURN_IF_ERR(err);

	TryLock();
	err = DoWalk(&DispatchClientWalkCallback, &context);
	TryUnlock();
	return err;
	}

TInt RAllocatorHelper::DoWalk(TWalkFunc3 aCallbackFn, TAny* aContext)
	{
	TInt err = KErrNotSupported;
	switch (iAllocatorType)
		{
		case EUdebOldRHeap:
		case EUrelOldRHeap:
			err = OldSkoolWalk(aCallbackFn, aContext);
			break;
		case EUrelHybridHeap:
		case EUdebHybridHeap:
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			err = NewHotnessWalk(aCallbackFn, aContext);
			break;
		default:
			err = KErrNotSupported;
			break;
		}
	return err;
	}

struct SContext
	{
	RAllocatorHelper::TWalkFunc iOrigWalkFn;
	TAny* iOrigContext;
	};

struct SContext2
	{
	RAllocatorHelper::TWalkFunc2 iOrigWalkFn;
	TAny* iOrigContext;
	};

#define New2Old(aNew) (((aNew)&RAllocatorHelper::EAllocationMask) ? RAllocatorHelper::EAllocation : ((aNew)&RAllocatorHelper::EFreeMask) ? RAllocatorHelper::EFreeSpace : RAllocatorHelper::EBadness)

TBool DispatchOldTWalkFuncCallback(RAllocatorHelper& /*aHelper*/, TAny* aContext, RAllocatorHelper::TExtendedCellType aCellType, TLinAddr aCellPtr, TInt aCellLength)
	{
	SContext* context = static_cast<SContext*>(aContext);
	return (*context->iOrigWalkFn)(context->iOrigContext, New2Old(aCellType), aCellPtr, aCellLength);
	}

TBool DispatchOldTWalk2FuncCallback(RAllocatorHelper& aHelper, TAny* aContext, RAllocatorHelper::TExtendedCellType aCellType, TLinAddr aCellPtr, TInt aCellLength)
	{
	SContext2* context = static_cast<SContext2*>(aContext);
	return (*context->iOrigWalkFn)(aHelper, context->iOrigContext, New2Old(aCellType), aCellPtr, aCellLength);
	}

HUEXPORT_C TInt RAllocatorHelper::Walk(TWalkFunc aCallbackFn, TAny* aContext)
	{
	// For backwards compatability insert a compatability callback to map between the different types of callback that clients requested
	SContext context = { aCallbackFn, aContext };
	return Walk(&DispatchOldTWalkFuncCallback, &context);
	}

HUEXPORT_C TInt RAllocatorHelper::Walk(TWalkFunc2 aCallbackFn, TAny* aContext)
	{
	SContext2 context = { aCallbackFn, aContext };
	return Walk(&DispatchOldTWalk2FuncCallback, &context);
	}


TInt RAllocatorHelper::OldSkoolWalk(TWalkFunc3 aCallbackFn, TAny* aContext)
	{
	TLinAddr pC = 0;
	TInt err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iBase), pC); // pC = iBase; // allocated cells
	RETURN_IF_ERR(err);
	TLinAddr pF = iAllocatorAddress + _FOFF(RHackHeap, iAlign) + 3*4; // pF = &iFree; // free cells

	TLinAddr top = 0;
	err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iTop), top);
	RETURN_IF_ERR(err);
	const TInt KAllocatedCellHeaderSize = iAllocatorType == EUdebOldRHeap ? 12 : 4;
	TInt minCell = 0;
	err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iAlign) + 4, (TUint32&)minCell);
	RETURN_IF_ERR(err);
	TInt align = 0;
	err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iAlign), (TUint32&)align);
	RETURN_IF_ERR(err);

	FOREVER
		{
		err = ReadWord(pF+4, pF); // pF = pF->next; // next free cell
		RETURN_IF_ERR(err);
		TLinAddr pFnext = 0;
		if (pF) err = ReadWord(pF + 4, pFnext);
		RETURN_IF_ERR(err);

		if (!pF)
			{
			pF = top; // to make size checking work
			}
		else if (pF>=top || (pFnext && pFnext<=pF) )
			{
			// free cell pointer off the end or going backwards
			//Unlock();
			(*aCallbackFn)(*this, aContext, EHeapBadFreeCellAddress, pF, 0);
			return KErrCorrupt;
			}
		else
			{
			TInt l; // = pF->len
			err = ReadWord(pF, (TUint32&)l);
			RETURN_IF_ERR(err);
			if (l<minCell || (l & (align-1)))
				{
				// free cell length invalid
				//Unlock();
				(*aCallbackFn)(*this, aContext, EHeapBadFreeCellSize, pF, l);
				return KErrCorrupt;
				}
			}
		
		while (pC!=pF)				// walk allocated cells up to next free cell
			{
			TInt l; // pC->len;
			err = ReadWord(pC, (TUint32&)l);
			RETURN_IF_ERR(err);
			if (l<minCell || (l & (align-1)))
				{
				// allocated cell length invalid
				//Unlock();
				(*aCallbackFn)(*this, aContext, EHeapBadAllocatedCellSize, pC, l);
				return KErrCorrupt;
				}
			TBool shouldContinue = (*aCallbackFn)(*this, aContext, EHeapAllocation, pC + KAllocatedCellHeaderSize, l - KAllocatedCellHeaderSize);
			if (!shouldContinue) return KErrNone;
			
			//SCell* pN = __NEXT_CELL(pC);
			TLinAddr pN = pC + l;
			if (pN > pF)
				{
				// cell overlaps next free cell
				//Unlock();
				(*aCallbackFn)(*this, aContext, EHeapBadAllocatedCellAddress, pC, l);
				return KErrCorrupt;
				}
			pC = pN;
			}
		if (pF == top)
			break;		// reached end of heap
		TInt pFlen = 0;
		err = ReadWord(pF, (TUint32&)pFlen);
		RETURN_IF_ERR(err);
		pC = pF + pFlen; // pC = __NEXT_CELL(pF);	// step to next allocated cell
		TBool shouldContinue = (*aCallbackFn)(*this, aContext, EHeapFreeCell, pF, pFlen);
		if (!shouldContinue) return KErrNone;
		}
	return KErrNone;
	}

HUEXPORT_C TInt RAllocatorHelper::CountUnusedPages()
	{
	TInt err = CheckValid(EUnusedPages);
	RETURN_IF_ERR(err);
	return iInfo->iUnusedPages;
	}

HUEXPORT_C TInt RAllocatorHelper::CommittedFreeSpace()
	{
	TInt err = CheckValid(ECommittedFreeSpace);
	RETURN_IF_ERR(err);
	return iInfo->iCommittedFreeSpace;
	}

#define ROUND_DOWN(val, pow2) ((val) & ~((pow2)-1))
#define ROUND_UP(val, pow2) ROUND_DOWN((val) + (pow2) - 1, (pow2))

HUEXPORT_C TLinAddr RAllocatorHelper::AllocatorAddress() const
	{
	return iAllocatorAddress;
	}

TBool RAllocatorHelper::WalkForStats(RAllocatorHelper& aSelf, TAny* /*aContext*/, TExtendedCellType aType, TLinAddr aCellPtr, TInt aCellLength)
	{
	//ASSERT(aCellLength >= 0);
	THeapInfo& info = *aSelf.iInfo;

	TInt pagesSpanned = 0; // The number of pages that fit entirely inside the payload of this cell
	if ((TUint)aCellLength > KPageSize)
		{
		TLinAddr nextPageAlignedAddr = ROUND_UP(aCellPtr, KPageSize);
		pagesSpanned = ROUND_DOWN(aCellPtr + aCellLength - nextPageAlignedAddr, KPageSize) / KPageSize;
		}

	if (aSelf.iAllocatorType == EUrelOldRHeap || aSelf.iAllocatorType == EUdebOldRHeap)
		{
		if (aType & EFreeMask)
			{
			info.iUnusedPages += pagesSpanned;
			info.iCommittedFreeSpace += aCellLength;
			info.iHeapFreeCellCount++;
			}
		}
	else
		{
		if (aType & EAllocationMask)
			{
			info.iAllocatedSize += aCellLength;
			info.iAllocationCount++;
			}
		else if (aType & EFreeMask)
			{
			// I *think* that DLA will decommit pages from inside free cells...
			TInt committedLen = aCellLength - (pagesSpanned * KPageSize);
			info.iCommittedFreeSpace += committedLen;
			}

		switch (aType)
			{
			case EDlaAllocation:
				info.iDlaAllocsSize += aCellLength;
				info.iDlaAllocsCount++;
				break;
			case EPageAllocation:
				info.iPageAllocsSize += aCellLength;
				info.iPageAllocsCount++;
				break;
			case ESlabAllocation:
				info.iSlabAllocsSize += aCellLength;
				info.iSlabAllocsCount++;
				break;
			case EDlaFreeCell:
				info.iDlaFreeSize += aCellLength;
				info.iDlaFreeCount++;
				break;
			case ESlabFreeCell:
				info.iSlabFreeCellSize += aCellLength;
				info.iSlabFreeCellCount++;
				break;
			case ESlabFreeSlab:
				info.iSlabFreeSlabSize += aCellLength;
				info.iSlabFreeSlabCount++;
				break;
			default:
				break;
			}
		}

	return ETrue;
	}

#define PAGESHIFT 12

TUint RAllocatorHelper::PageMapOperatorBrackets(unsigned ix, TInt& err) const
	{
	//return 1U&(iBase[ix>>3] >> (ix&7));
	TUint32 basePtr = 0;
	err = ReadWord(iAllocatorAddress + PageMapOffset(), basePtr);
	if (err) return 0;

	TUint8 res = 0;
	err = ReadByte(basePtr + (ix >> 3), res);
	if (err) return 0;

	return 1U&(res >> (ix&7));
	}


TInt RAllocatorHelper::PageMapFind(TUint start, TUint bit, TInt& err)
	{
	TUint32 iNbits = 0;
	err = ReadWord(iAllocatorAddress + PageMapOffset() + 4, iNbits);
	if (err) return 0;

	if (start<iNbits) do
		{
		//if ((*this)[start]==bit)
		if (PageMapOperatorBrackets(start, err) == bit || err)
			return start;
		} while (++start<iNbits);
	return -1;
	}

TUint RAllocatorHelper::PagedDecode(TUint pos, TInt& err)
	{
	unsigned bits = PageMapBits(pos,2,err);
	if (err) return 0;
	bits >>= 1;
	if (bits == 0)
		return 1;
	bits = PageMapBits(pos+2,2,err);
	if (err) return 0;
	if ((bits & 1) == 0)
		return 2 + (bits>>1);
	else if ((bits>>1) == 0)
		{
		return PageMapBits(pos+4, 4,err);
		}
	else
		{
		return PageMapBits(pos+4, 18,err);
		}
	}

TUint RAllocatorHelper::PageMapBits(unsigned ix, unsigned len, TInt& err)
	{
	int l=len;
	unsigned val=0;
	unsigned bit=0;
	while (--l>=0)
		{
		//val |= (*this)[ix++]<<bit++;
		val |= PageMapOperatorBrackets(ix++, err) << bit++;
		if (err) return 0;
		}
	return val;
	}

enum TSlabType { ESlabFullInfo, ESlabPartialInfo, ESlabEmptyInfo };

TInt RAllocatorHelper::NewHotnessWalk(TWalkFunc3 aCallbackFn, TAny* aContext)
	{
	// RHybridHeap does paged, slab then DLA, so that's what we do too
	// Remember Kernel RHybridHeaps don't even have the page and slab members

	TUint32 basePtr;
	TInt err = ReadWord(iAllocatorAddress + _FOFF(RHackHeap, iBase), basePtr);
	RETURN_IF_ERR(err);
	if (basePtr < iAllocatorAddress + UserInitialHeapMetaDataSize())
		{
		// Must be a kernel one - don't do page and slab
		LOG("RAllocatorHelper::NewHotnessWalk() - kernel heap, no paged and slab cells");
		}
	else
		{
		// Paged
		LOG("RAllocatorHelper::NewHotnessWalk() - paged cells");
		TUint32 membase = 0;
		err = ReadWord(iAllocatorAddress + MemBaseOffset(), membase);
		RETURN_IF_ERR(err);

		TBool shouldContinue = ETrue;
		for (int ix = 0;(ix = PageMapFind(ix,1,err)) >= 0 && err == KErrNone;)
			{
			int npage = PagedDecode(ix, err);
			RETURN_IF_ERR(err);
			// Introduce paged buffer to the walk function 
			TLinAddr bfr = membase + (1 << (PAGESHIFT-1))*ix;
			int len = npage << PAGESHIFT;
			if ( (TUint)len > KPageSize )
				{ // If buffer is not larger than one page it must be a slab page mapped into bitmap
				switch (iAllocatorType)
					{
					case EUdebHybridHeap:
					case EUdebHybridHeapQt:
					case EUdebHybridHeapV2:
						bfr += 8;
						len -= 8;
					}
				shouldContinue = (*aCallbackFn)(*this, aContext, EPageAllocation, bfr, len);
				if (!shouldContinue) return KErrNone;
				}
			ix += (npage<<1);
			}
		RETURN_IF_ERR(err);

		// Slab
		LOG("RAllocatorHelper::NewHotnessWalk() - slab cells");
		TUint32 sparePage = 0;
		err = ReadWord(iAllocatorAddress + SparePageOffset(), sparePage);
		RETURN_IF_ERR(err);
		if (sparePage)
			{
			//Walk(wi, iSparePage, iPageSize, EGoodFreeCell, ESlabSpare); // Introduce Slab spare page to the walk function 
			// This counts as 4 spare slabs
			for (TInt i = 0; i < 4; i++)
				{
				shouldContinue = (*aCallbackFn)(*this, aContext, ESlabFreeSlab, sparePage + SLABSIZE*i, SLABSIZE);
				if (!shouldContinue) return KErrNone;
				}
			}

		//TreeWalk(&iFullSlab, &SlabFullInfo, i, wi);
		LOG("RAllocatorHelper::NewHotnessWalk() - full slab traversal");
		TInt err = TreeWalk(iAllocatorAddress + FullSlabOffset(), ESlabFullInfo, aCallbackFn, aContext, shouldContinue);
		if (err || !shouldContinue) return err;
		for (int ix = 0; ix < (MAXSLABSIZE>>2); ++ix)
			{
			TUint32 partialAddr = iAllocatorAddress + SlabAllocOffset() + ix*HybridCom::KSlabsetSize;
			LOG("RAllocatorHelper::NewHotnessWalk() - partial slab traversal, index: %d, slabset address: 0x%08x", ix, partialAddr );
			//TreeWalk(&iSlabAlloc[ix].iPartial, &SlabPartialInfo, i, wi);
			err = TreeWalk(partialAddr, ESlabPartialInfo, aCallbackFn, aContext, shouldContinue);
			LOG("RAllocatorHelper::NewHotnessWalk() - partial slab traversal finished, index: %d, slabset address: 0x%08x", ix, partialAddr );
			if (err || !shouldContinue) return err;
			}
		//TreeWalk(&iPartialPage, &SlabEmptyInfo, i, wi);
		TreeWalk(iAllocatorAddress + PartialPageOffset(), ESlabEmptyInfo, aCallbackFn, aContext, shouldContinue);
		}

	// DLA

#ifndef DLA_H_INCLUDED // If we haven't included dla.h (which we do for purposes of testing the asserts) we need to define these ourselves
#define CHUNK_OVERHEAD (sizeof(TUint))
#define CHUNK_ALIGN_MASK (7)
#define CINUSE_BIT 2
#define INUSE_BITS 3
#endif // DLA_H_INCLUDED

#define CHUNK2MEM(p)		((TLinAddr)(p) + 8)
#define MEM2CHUNK(mem)	  ((TLinAddr)(p) - 8)
/* chunk associated with aligned address A */
#define ALIGN_OFFSET(A)\
	((((TLinAddr)(A) & CHUNK_ALIGN_MASK) == 0)? 0 :\
	((8 - ((TLinAddr)(A) & CHUNK_ALIGN_MASK)) & CHUNK_ALIGN_MASK))
#define ALIGN_AS_CHUNK(A)   ((A) + ALIGN_OFFSET(CHUNK2MEM(A)))

	LOG("RAllocatorHelper::NewHotnessWalk() - dla cells");
	TUint32 topSize = 0;
	err = ReadWord(iAllocatorAddress + MallocStateOffset() + HybridCom::KMallocStateTopSizeOffset, topSize);
	RETURN_IF_ERR(err);

	TUint32 top = 0;
	err = ReadWord(iAllocatorAddress + MallocStateOffset() + HybridCom::KMallocStateTopOffset, top);
	RETURN_IF_ERR(err);

	TInt max = ((topSize-1) & ~CHUNK_ALIGN_MASK) - CHUNK_OVERHEAD;
	if ( max < 0 )
		max = 0;
	
	TBool shouldContinue = (*aCallbackFn)(*this, aContext, EDlaFreeCell, top, max);
	if (!shouldContinue) return KErrNone;
	
	TUint32 mallocStateSegBase = 0;
	err = ReadWord(iAllocatorAddress + MallocStateOffset() + HybridCom::KMallocStateSegOffset, mallocStateSegBase);
	RETURN_IF_ERR(err);

	for (TLinAddr q = ALIGN_AS_CHUNK(mallocStateSegBase); q != top; /*q = NEXT_CHUNK(q)*/)
		{
		TUint32 qhead = 0;
		err = ReadWord(q + 4, qhead);
		RETURN_IF_ERR(err);
		//TInt sz = CHUNKSIZE(q);
		TInt sz = qhead & ~(INUSE_BITS);
		if (!(qhead & CINUSE_BIT))
			{
			//Walk(wi, CHUNK2MEM(q), sz, EGoodFreeCell, EDougLeaAllocator); // Introduce DL free buffer to the walk function 
			shouldContinue = (*aCallbackFn)(*this, aContext, EDlaFreeCell, CHUNK2MEM(q), sz);
			if (!shouldContinue) return KErrNone;
			}
		else
			{
			//Walk(wi, CHUNK2MEM(q), (sz- CHUNK_OVERHEAD), EGoodAllocatedCell, EDougLeaAllocator); // Introduce DL allocated buffer to the walk function 
			TLinAddr addr = CHUNK2MEM(q);
			TInt size = sz - CHUNK_OVERHEAD;
			switch (iAllocatorType)
				{
				case EUdebHybridHeap:
				case EUdebHybridHeapQt:
				case EUdebHybridHeapV2:
					size -= 8;
					addr += 8;
				}
			shouldContinue = (*aCallbackFn)(*this, aContext, EDlaAllocation, addr, size);
			if (!shouldContinue) return KErrNone;
			}
		// This is q = NEXT_CHUNK(q) expanded
		q = q + sz;
		}
	return KErrNone;
	}

TInt RAllocatorHelper::TreeWalk(TUint32 aSlabRoot, TInt aSlabType, TWalkFunc3 aCallbackFn, TAny* aContext, TBool& shouldContinue)
	{
	const TSlabType type = (TSlabType)aSlabType;
	LOG("RAllocatorHelper::TreeWalk() - START, slab type: %d", type );

	TUint32 s = 0;
	TInt err = ReadWord(aSlabRoot, s);
	RETURN_IF_ERR(err);
	//slab* s = *root;
	LOG("RAllocatorHelper::TreeWalk() - root at: 0x%08x, s: 0x%08x", aSlabRoot, s );
	if (!s)
		return KErrNone;
	
	for (;;)
		{
		//slab* c;
		//while ((c = s->iChild1) != 0)
		//	s = c;		// walk down left side to end
		TUint32 c;
		for(;;)
			{
			err = ReadWord(s + HybridCom::KSlabChild1Offset, c);
			LOG("RAllocatorHelper::TreeWalk() - walking down left side, current: 0x%08x, error: %d", c, err );
			RETURN_IF_ERR(err);
			if (c == 0) break;
			else s = c;
			}
		for (;;)
			{
			//TODOf(s, i, wi);
			//TODO __HEAP_CORRUPTED_TEST_STATIC
			TUint32 h;
			err = ReadWord(s, h); // = aSlab->iHeader;
			RETURN_IF_ERR(err);
			TUint32 size = (h&0x0003f000)>>12; //SlabHeaderSize(h);
			LOG("RAllocatorHelper::TreeWalk() - slab header at: 0x%08x, size is %d", h, size );
			TUint debugheadersize = 0;
			switch (iAllocatorType)
				{
				case EUdebHybridHeap:
				case EUdebHybridHeapQt:
				case EUdebHybridHeapV2:
					debugheadersize = 8;
				}
			TUint32 usedCount = (((h&0x0ffc0000)>>18) + 4) / size; // (SlabHeaderUsedm4(h) + 4) / size;
			switch (type)
				{
				case ESlabFullInfo:
					{
					TUint32 count = usedCount;
					TUint32 i = 0;
					while ( i < count )
						{
						TUint32 addr = s + HybridCom::KSlabPayloadOffset + SlabPadding(size) + i*size; //&aSlab->iPayload[i*size];
						shouldContinue = (*aCallbackFn)(*this, aContext, ESlabAllocation, addr + debugheadersize, size - debugheadersize);
						if (!shouldContinue) return KErrNone;
						i++;
						}
					break;
					}
				case ESlabPartialInfo:
					{
					//TODO __HEAP_CORRUPTED_TEST_STATIC
					TUint32 count = HybridCom::KMaxSlabPayload / size;
					TUint32 freeOffset = (h & 0xff) << 2;
					if (freeOffset == 0)
						{
						// TODO Shouldn't happen for a slab on the partial list
						}
					memset(iTempSlabBitmap, 1, KTempBitmapSize); // Everything defaults to in use
					TUint wildernessCount = count - usedCount;
					while (freeOffset)
						{
						wildernessCount--;
						TInt idx = (freeOffset - HybridCom::KSlabPayloadOffset - SlabPadding(size)) / size;
						LOG("iTempSlabBitmap freeOffset %d index %d", freeOffset, idx);
						iTempSlabBitmap[idx] = 0; // Mark it as free

						TUint32 addr = s + freeOffset;
						TUint8 nextCell = 0;
						err = ReadByte(addr, nextCell);
						RETURN_IF_ERR(err);
						freeOffset = ((TUint32)nextCell) << 2;
						}
					memset(iTempSlabBitmap + count - wildernessCount, 0, wildernessCount); // Mark the wilderness as free
					for (TInt i = 0; i < count; i++)
						{
						TLinAddr addr = s + HybridCom::KSlabPayloadOffset + SlabPadding(size) + i*size;
						if (iTempSlabBitmap[i])
							{
							// In use
							shouldContinue = (*aCallbackFn)(*this, aContext, ESlabAllocation, addr + debugheadersize, size - debugheadersize);
							}
						else
							{
							// Free
							shouldContinue = (*aCallbackFn)(*this, aContext, ESlabFreeCell, addr, size);
							}
						if (!shouldContinue) return KErrNone;
						}
					break;
					}
				case ESlabEmptyInfo:
					{
					// Check which slabs of this page are empty
					TUint32 pageAddr = ROUND_DOWN(s, KPageSize);
					TUint32 headerForPage = 0;
					err = ReadWord(pageAddr, headerForPage);
					RETURN_IF_ERR(err);
					TUint32 slabHeaderPageMap = (headerForPage & 0x00000f00)>>8; // SlabHeaderPagemap(unsigned h)
					for (TInt slabIdx = 0; slabIdx < 4; slabIdx++)
						{
						if (slabHeaderPageMap & (1<<slabIdx))
							{
							TUint32 addr = pageAddr + SLABSIZE*slabIdx + HybridCom::KSlabPayloadOffset; //&aSlab->iPayload[i*size];
							shouldContinue = (*aCallbackFn)(*this, aContext, ESlabFreeSlab, addr, HybridCom::KMaxSlabPayload);
							if (!shouldContinue) return KErrNone;
							}
						}
					break;
					}
				}

			//c = s->iChild2;
			err = ReadWord(s + HybridCom::KSlabChild2Offset, c);
			RETURN_IF_ERR(err);

			if (c)
				{	// one step down right side, now try and walk down left
				s = c;
				break;
				}
			for (;;)
				{	// loop to walk up right side
				TUint32 pp = 0;
				err = ReadWord(s + HybridCom::KSlabParentOffset, pp);
				RETURN_IF_ERR(err);
				//slab** pp = s->iParent;
				if (pp == aSlabRoot)
					return KErrNone;
#define SlabFor(x) ROUND_DOWN(x, SLABSIZE)
				s = SlabFor(pp);
				//if (pp == &s->iChild1)
				if (pp == s + HybridCom::KSlabChild1Offset)
					break;
				}
			}
		}
	
	//LOG("RAllocatorHelper::TreeWalk() - END");
	}

// Really should be called TotalSizeForCellType(...)
HUEXPORT_C TInt RAllocatorHelper::SizeForCellType(TExtendedCellType aType)
	{
	if (aType & EBadnessMask) return KErrArgument;
	if (aType == EAllocationMask) return AllocatedSize();

	//if (iAllocatorType == EUdebOldRHeap || iAllocatorType == EUrelOldRHeap)
	switch (iAllocatorType)
		{
	case EUdebOldRHeap:
	case EUrelOldRHeap:
		{
		switch (aType)
			{
			case EHeapAllocation:
				return AllocatedSize();
			case EHeapFreeCell:
			case EFreeMask:
				return CommittedFreeSpace();
			default:
				return KErrNotSupported;
			}
		}
	case EUrelHybridHeap:
	case EUdebHybridHeap:
	case EUrelHybridHeapV2:
	case EUdebHybridHeapV2:
	case EUrelHybridHeapQt:
	case EUdebHybridHeapQt:
		{
		TInt err = CheckValid(EHybridStats);
		RETURN_IF_ERR(err);

		switch (aType)
			{
			case EHeapAllocation:
			case EHeapFreeCell:
				return KErrNotSupported;
			case EDlaAllocation:
				return iInfo->iDlaAllocsSize;
			case EPageAllocation:
				return iInfo->iPageAllocsSize;
			case ESlabAllocation:
				return iInfo->iSlabAllocsSize;
			case EDlaFreeCell:
				return iInfo->iDlaFreeSize;
			case ESlabFreeCell:
				return iInfo->iSlabFreeCellSize;
			case ESlabFreeSlab:
				return iInfo->iSlabFreeSlabSize;
			case EFreeMask:
				// Note this isn't the same as asking for CommittedFreeSpace(). SizeForCellType(EFreeMask) may include decommitted pages that lie inside a free cell
				return iInfo->iDlaFreeSize + iInfo->iSlabFreeCellSize + iInfo->iSlabFreeSlabSize;
			default:
				return KErrNotSupported;
			}
		}
	default:
		return KErrNotSupported;
		}
	}

HUEXPORT_C TInt RAllocatorHelper::CountForCellType(TExtendedCellType aType)
	{
	if (aType & EBadnessMask) return KErrArgument;
	if (aType == EAllocationMask) return AllocationCount();

	switch (iAllocatorType)
		{
	case EUdebOldRHeap:
	case EUrelOldRHeap:
		{
		switch (aType)
			{
			case EHeapAllocation:
				return AllocationCount();
			case EHeapFreeCell:
			case EFreeMask:
				{
				TInt err = CheckValid(ECommittedFreeSpace);
				RETURN_IF_ERR(err);
				return iInfo->iHeapFreeCellCount;
				}
			default:
				return KErrNotSupported;
			}
		}
	case EUrelHybridHeap:
	case EUdebHybridHeap:
	case EUrelHybridHeapV2:
	case EUdebHybridHeapV2:
	case EUrelHybridHeapQt:
	case EUdebHybridHeapQt:
		{
		TInt err = CheckValid(EHybridStats);
		RETURN_IF_ERR(err);

		switch (aType)
			{
			case EHeapAllocation:
			case EHeapFreeCell:
				return KErrNotSupported;
			case EDlaAllocation:
				return iInfo->iDlaAllocsCount;
			case EPageAllocation:
				return iInfo->iPageAllocsCount;
			case ESlabAllocation:
				return iInfo->iSlabAllocsCount;
			case EDlaFreeCell:
				return iInfo->iDlaFreeCount;
			case ESlabFreeCell:
				return iInfo->iSlabFreeCellCount;
			case ESlabFreeSlab:
				return iInfo->iSlabFreeSlabCount;
			case EFreeMask:
				// This isn't a hugely meaningful value, but if that's what they asked for...
				return iInfo->iDlaFreeCount + iInfo->iSlabFreeCellCount + iInfo->iSlabFreeSlabCount;
			default:
				return KErrNotSupported;
			}
		}
	default:
		return KErrNotSupported;
		}
	}

HUEXPORT_C TBool LtkUtils::RAllocatorHelper::AllocatorIsUdeb() const
	{
	return iAllocatorType == EUdebOldRHeap || iAllocatorType == EUdebHybridHeap || iAllocatorType == EUdebHybridHeapV2 || iAllocatorType == EUdebHybridHeapQt;
	}


HUEXPORT_C const TDesC& LtkUtils::RAllocatorHelper::Description() const
	{
	_LIT(KRHeap, "RHeap");
	_LIT(KRHybridHeap, "RHybridHeap");
	_LIT(KRHybridHeapV2, "RHybridHeap v2");
	_LIT(KRHybridHeapQt, "RHybridHeap Qt");
	_LIT(KUnknown, "Unknown");
	switch (iAllocatorType)
		{
		case EUrelOldRHeap:
		case EUdebOldRHeap:
			return KRHeap;
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return KRHybridHeap;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return KRHybridHeapV2;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return KRHybridHeapQt;
		case EAllocatorUnknown:
		case EAllocatorNotSet:
		default:
			return KUnknown;
		}
	}

#ifdef __KERNEL_MODE__

DChunk* LtkUtils::RAllocatorHelper::OpenUnderlyingChunk()
	{
	// Enter and leave in CS and with no locks held. On exit the returned DChunk has been Open()ed.
	if( iChunk )
		{
		TInt err = iChunk->Open();
		if (err) return NULL;
		}
	return iChunk;
	}

DChunk* LtkUtils::RUserAllocatorHelper::OpenUnderlyingChunk()
	{
	if (iAllocatorType != EUrelOldRHeap && iAllocatorType != EUdebOldRHeap && 
		iAllocatorType != EUrelHybridHeap && iAllocatorType != EUdebHybridHeap &&
		iAllocatorType != EUrelHybridHeapV2 && iAllocatorType != EUdebHybridHeapV2 &&
		iAllocatorType != EUrelHybridHeapQt && iAllocatorType != EUdebHybridHeapQt)
		{
		return NULL;
		}
	
	// Note RUserAllocatorHelper doesn't use or access RAllocatorHelper::iChunk, because we figure out the chunk handle in a different way.
	// It is for this reason that iChunk is private, to remove temptation
	
	// Enter and leave in CS and with no locks held. On exit the returned DChunk has been Open()ed.
	TUint32 chunkHandle = 0;
	TInt err = ReadData(iAllocatorAddress + _FOFF(RHackHeap, iChunkHandle), &chunkHandle, sizeof(TUint32));
	if (err) return NULL;

	NKern::LockSystem();
	DChunk* result = (DChunk*)Kern::ObjectFromHandle(iThread, chunkHandle, EChunk);
	if (result && result->Open() != KErrNone)
		{
		result = NULL;
		}
	NKern::UnlockSystem();
	return result;
	}

LtkUtils::RAllocatorHelper::TType LtkUtils::RAllocatorHelper::GetType() const
	{
	switch (iAllocatorType)
		{
		case EUrelOldRHeap:
		case EUdebOldRHeap:
			return ETypeRHeap;
		case EUrelHybridHeap:
		case EUdebHybridHeap:
			return ETypeRHybridHeap;
		case EUrelHybridHeapV2:
		case EUdebHybridHeapV2:
			return ETypeRHybridHeapV2;
		case EUrelHybridHeapQt:
		case EUdebHybridHeapQt:
			return ETypeRHybridHeapQt;
		case EAllocatorUnknown:
		case EAllocatorNotSet:
		default:
			return ETypeUnknown;
		}
	}

#else

TInt LtkUtils::RAllocatorHelper::EuserIsUdeb()
	{
	TAny* buf = User::Alloc(4096);
	if (!buf) return KErrNoMemory;
	RAllocator* dummyHeap = UserHeap::FixedHeap(buf, 4096, 4, ETrue);
	if (!dummyHeap) 
		{
		User::Free(buf);
		return KErrNoMemory; // Don't think this can happen
		}

	dummyHeap->__DbgSetAllocFail(RAllocator::EFailNext, 1);
	TAny* ptr = dummyHeap->Alloc(4);
	// Because we specified singleThreaded=ETrue we can allow dummyHeap to just go out of scope here
	User::Free(buf);

	if (ptr)
		{
		// Clearly the __DbgSetAllocFail had no effect so we must be urel
		// We don't need to free ptr because it came from the dummy heap
		return EFalse;
		}
	else
		{
		return ETrue;
		}
	}

#ifndef STANDALONE_ALLOCHELPER

#include <fshell/ltkutils.h>
HUEXPORT_C void LtkUtils::MakeHeapCellInvisible(TAny* aCell)
	{
	RAllocatorHelper helper;
	TInt err = helper.Open(&User::Allocator());
	if (err == KErrNone)
		{
		helper.SetCellNestingLevel(aCell, -1);
		helper.Close();
		}
	}
#endif // STANDALONE_ALLOCHELPER

#endif
