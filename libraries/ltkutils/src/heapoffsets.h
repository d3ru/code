// heapoffsets.h
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
// Adrian Issott (Nokia) - Updates for RHybridHeap v2
//

#ifndef FSHELL_HEAP_OFFSETS_H
#define FSHELL_HEAP_OFFSETS_H

#include <e32def.h>

#if defined(TEST_HYBRIDHEAP_V1_ASSERTS) || defined(TEST_HYBRIDHEAP_V2_ASSERTS)
#define TEST_HYBRIDHEAP_COMMON_ASSERTS
#endif

#ifdef TEST_HYBRIDHEAP_COMMON_ASSERTS
#define private public
#include <e32def.h>
#endif 

#ifdef TEST_HYBRIDHEAP_V2_ASSERTS
#include <e32panic.h>
#include <kernel/heap_hybrid.h>
#include "common.h"
#include "heap_priv_defs.h"
#include "dla.h"
#define DLA_H_INCLUDED
#endif

#ifdef TEST_HYBRIDHEAP_COMMON_ASSERTS
#include "slab.h"
#include "page_alloc.h"
#endif

#ifdef TEST_HYBRIDHEAP_V1_ASSERTS
#include <kernel/heap_hybrid.h>
#endif


/*
 * This header provides offsets for use with three different types of heap algorithms:
 * i) The original RHeap implemenation 
 *	(RHeap v1)
 * ii) The first RHybridHeap implemenation 
 *	(RHeap v2 + RHybridHeap v1)
 * iii) The second / refactored RHybridHeap implementation 
 *	  (RHeap v2 + RHybridHeap v2 + RDlAllocator + RPageAllocator + RSlabHeapAllocator)
 */
namespace LtkUtils
	{

//
// OFFSETS COMMON TO ALL THREE HEAP CLASSES
// 

class RHackAllocator : public RAllocator
	{
public:
	using RAllocator::iHandles;
	using RAllocator::iTotalAllocSize;
	using RAllocator::iCellCount;
	};

class RHackHeap : public RHeap
	{
public:
	// Careful, only allowed to use things that are in the same place for versions 1 and 2 of RHeap
	using RHeap::iMaxLength;
	using RHeap::iChunkHandle;
	using RHeap::iLock;
	using RHeap::iBase;
	using RHeap::iAlign;
	using RHeap::iTop;
	};

const TInt KChunkSizeOffset = 30*4;

//
// OFFSETS SPECIFIC TO THE RHEAP V1 CLASS
// 

namespace HeapV1
	{

const TInt KUserInitialHeapMetaDataSize = 34*4;
		
	} // namespace HeapV1

//
// OFFSETS COMMON TO BOTH RHYBRIDHEAP CLASSES
// 

namespace HybridCom
	{

const TInt KMallocStateTopSizeOffset = 3*4;
const TInt KMallocStateTopOffset = 5*4;
const TInt KMallocStateSegOffset = 105*4;
const TInt KSlabParentOffset = 1*4;
const TInt KSlabChild1Offset = 2*4;
const TInt KSlabChild2Offset = 3*4;
const TInt KSlabPayloadOffset = 4*4;
const TInt KSlabsetSize = 4;
//const TInt KDlOnlyOffset = 33*4;

#define MAXSLABSIZE	 56
#define SLABSHIFT	   10
#define SLABSIZE		(1 << SLABSHIFT)
const TInt KMaxSlabPayload = SLABSIZE - KSlabPayloadOffset;

#ifdef TEST_HYBRIDHEAP_COMMON_ASSERTS
__ASSERT_COMPILE(_FOFF(RHybridHeap, iChunkSize) == KChunkSizeOffset);
__ASSERT_COMPILE(sizeof(malloc_state) == 107*4);
#endif

	} // namespace HybridCom

//
// OFFSETS SPECIFIC TO THE RHYBRIDHEAP V1 CLASS
// 

namespace HybridV1
	{

const TInt KUserPageMapOffset = 141*4;
const TInt KUserMemBaseOffset = 143*4;
const TInt KMallocStateOffset = 34*4; // same for user and kernel heaps
const TInt KUserSparePageOffset = 167*4;
const TInt KUserPartialPageOffset = 165*4;
const TInt KUserFullSlabOffset = 166*4;
const TInt KUserSlabAllocOffset = 172*4;

const TInt KUserInitialHeapMetaDataSize = 186*4;

__ASSERT_COMPILE(HeapV1::KUserInitialHeapMetaDataSize < KUserInitialHeapMetaDataSize);

#ifdef TEST_HYBRIDHEAP_V1_ASSERTS

const TInt KUserHybridHeapSize = KUserInitialHeapMetaDataSize;

#ifndef __KERNEL_MODE__
__ASSERT_COMPILE(sizeof(RHybridHeap) == KUserHybridHeapSize);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iPageMap) == KUserPageMapOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iMemBase) == KUserMemBaseOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iSparePage) == KUserSparePageOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iPartialPage) == KUserPartialPageOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iSlabAlloc) == KUserSlabAllocOffset);
__ASSERT_COMPILE(_FOFF(slab, iParent) == HybridCom::KSlabParentOffset);
__ASSERT_COMPILE(_FOFF(slab, iChild1) == HybridCom::KSlabChild1Offset);
__ASSERT_COMPILE(_FOFF(slab, iChild2) == HybridCom::KSlabChild2Offset);
__ASSERT_COMPILE(_FOFF(slab, iPayload) == HybridCom::KSlabPayloadOffset);
__ASSERT_COMPILE(sizeof(slabset) == HybridCom::KSlabsetSize);
#endif
__ASSERT_COMPILE(_FOFF(RHybridHeap, iGlobalMallocState) == KMallocStateOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iTopSize) == HybridCom::KMallocStateTopSizeOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iTop) == HybridCom::KMallocStateTopOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iSeg) == HybridCom::KMallocStateSegOffset);

#endif

	} // namespace HybridV1

//
// OFFSETS SPECIFIC TO THE RHYBRIDHEAP V2 CLASSES
// 

namespace HybridV2
	{

const TInt KUserPageMapOffset = 153*4;
const TInt KUserMemBaseOffset = 36*4;
const TInt KUserMallocStateOffset = 45*4;
const TInt KKernelMallocStateOffset = 37*4;
const TInt KUserSparePageOffset = 175*4;
const TInt KUserPartialPageOffset = 173*4;
const TInt KUserFullSlabOffset = 174*4;
const TInt KUserSlabAllocOffset = 180*4;
const TInt KSelfReferenceOffset = 34*4; // same for user and kernel heaps
const TInt KUserInitialHeapMetaDataSize = 195 * 4; //  A previous incarnation of RHybridHeapV2 seemed to have this as 194 due to KUserSlabAllocatorSize being smaller
const TInt KKernelInitialHeapMetaDataSize = 144 * 4;

__ASSERT_COMPILE(HeapV1::KUserInitialHeapMetaDataSize < KUserInitialHeapMetaDataSize);
__ASSERT_COMPILE(HeapV1::KUserInitialHeapMetaDataSize < KKernelInitialHeapMetaDataSize);

// The code relies on the following so we just check it's actually true here 
__ASSERT_COMPILE(KSelfReferenceOffset == HybridV1::KMallocStateOffset);

#ifdef TEST_HYBRIDHEAP_V2_ASSERTS

const TInt KUserHybridHeapSize = 44*4;
const TInt KUserDlAllocatorOffset = KUserHybridHeapSize;
const TInt KUserPageAllocatorSize = 20*4;
const TInt KUserSlabAllocatorSize = 23*4; // A previous incarnation of RHybridHeapV2 seemed to have this as 22 not 23.
const TInt KUserPageAllocatorOffset = 152 * 4;
const TInt KUserSlabAllocatorOffset = KUserPageAllocatorOffset + KUserPageAllocatorSize;

const TInt KKernelHybridHeapSize = 36*4;
const TInt KKernelDlAllocatorOffset = KKernelHybridHeapSize;


#ifndef __KERNEL_MODE__
__ASSERT_COMPILE(sizeof(RHybridHeap) == KUserHybridHeapSize);
__ASSERT_COMPILE(KUserDlAllocatorOffset + sizeof(RDlAllocator) == KUserPageAllocatorOffset);
__ASSERT_COMPILE(KUserHybridHeapSize + _FOFF(RDlAllocator, iDlaState) == KUserMallocStateOffset);
__ASSERT_COMPILE(sizeof(RPageAllocator) == KUserPageAllocatorSize);
__ASSERT_COMPILE(sizeof(RSlabHeapAllocator) == KUserSlabAllocatorSize);
__ASSERT_COMPILE(sizeof(RHybridHeap) + sizeof(RDlAllocator) + sizeof(RPageAllocator) + sizeof(RSlabHeapAllocator) == KUserInitialHeapMetaDataSize);
__ASSERT_COMPILE(KUserPageAllocatorOffset + _FOFF(RPageAllocator, iPageMap) == KUserPageMapOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iMemBase) == KUserMemBaseOffset);
__ASSERT_COMPILE(KUserSlabAllocatorOffset + _FOFF(RSlabHeapAllocator, iSparePage) == KUserSparePageOffset);
__ASSERT_COMPILE(KUserSlabAllocatorOffset + _FOFF(RSlabHeapAllocator, iPartialPage) == KUserPartialPageOffset);
__ASSERT_COMPILE(KUserSlabAllocatorOffset + _FOFF(RSlabHeapAllocator, iSlabAlloc) == KUserSlabAllocOffset);
__ASSERT_COMPILE(_FOFF(TSlab, iParent) == HybridCom::KSlabParentOffset);
__ASSERT_COMPILE(_FOFF(TSlab, iChild1) == HybridCom::KSlabChild1Offset);
__ASSERT_COMPILE(_FOFF(TSlab, iChild2) == HybridCom::KSlabChild2Offset);
__ASSERT_COMPILE(_FOFF(TSlab, iPayload) == HybridCom::KSlabPayloadOffset);
__ASSERT_COMPILE(sizeof(TSlabSet) == HybridCom::KSlabsetSize);
#else
__ASSERT_COMPILE(sizeof(RHybridHeap) == KKernelHybridHeapSize);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iHybridHeap) == KSelfReferenceOffset);
__ASSERT_COMPILE(_FOFF(RDlAllocator, iDlaState) == KKernelMallocStateOffset - KKernelHybridHeapSize);
#endif // __KERNEL_MODE__
__ASSERT_COMPILE(_FOFF(malloc_state, topsize) == HybridCom::KMallocStateTopSizeOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, top) == HybridCom::KMallocStateTopOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, seg) == HybridCom::KMallocStateSegOffset);

#endif

	} // namespace HybridV2

//
// OFFSETS SPECIFIC TO THE RHYBRIDHEAP Qt CLASSES
// 

namespace HybridQt
	{
const TInt KUserPageMapOffset = 150*4;
const TInt KUserMemBaseOffset = 152*4;
const TInt KMallocStateOffset = 43*4; // same for user and kernel heaps
const TInt KUserSparePageOffset = 176*4;
const TInt KUserPartialPageOffset = 174*4;
const TInt KUserFullSlabOffset = 175*4;
const TInt KUserSlabAllocOffset = 181*4;

const TInt KUserInitialHeapMetaDataSize = 195*4;

__ASSERT_COMPILE(HeapV1::KUserInitialHeapMetaDataSize < KUserInitialHeapMetaDataSize);
__ASSERT_COMPILE(HybridV1::KUserInitialHeapMetaDataSize < KUserInitialHeapMetaDataSize);

#ifdef TEST_HYBRIDHEAP_QT_ASSERTS

const TInt KUserHybridHeapSize = KUserInitialHeapMetaDataSize;

#ifndef __KERNEL_MODE__
__ASSERT_COMPILE(sizeof(RHybridHeap) == KUserHybridHeapSize);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iPageMap) == KUserPageMapOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iMemBase) == KUserMemBaseOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iSparePage) == KUserSparePageOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iPartialPage) == KUserPartialPageOffset);
__ASSERT_COMPILE(_FOFF(RHybridHeap, iSlabAlloc) == KUserSlabAllocOffset);
__ASSERT_COMPILE(_FOFF(slab, iParent) == HybridCom::KSlabParentOffset);
__ASSERT_COMPILE(_FOFF(slab, iChild1) == HybridCom::KSlabChild1Offset);
__ASSERT_COMPILE(_FOFF(slab, iChild2) == HybridCom::KSlabChild2Offset);
__ASSERT_COMPILE(_FOFF(slab, iPayload) == HybridCom::KSlabPayloadOffset);
__ASSERT_COMPILE(sizeof(slabset) == HybridCom::KSlabsetSize);
#endif
__ASSERT_COMPILE(_FOFF(RHybridHeap, iGlobalMallocState) == KMallocStateOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iTopSize) == HybridCom::KMallocStateTopSizeOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iTop) == HybridCom::KMallocStateTopOffset);
__ASSERT_COMPILE(_FOFF(malloc_state, iSeg) == HybridCom::KMallocStateSegOffset);

#endif

	} // namespace HybridQt

	} // namespace LtkUtils

#endif // FSHELL_HEAP_OFFSETS_H
