// memmap.cpp
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

#include <e32rom.h>
#include <fshell/ltkutils.h>
#include "memmap.h"

#ifdef FSHELL_FLEXIBLEMM_AWARE
#ifdef SYMBIAN_ENABLE_SPLIT_HEADERS
#undef SYMBIAN_ENABLE_SPLIT_HEADERS
#endif
#include <e32ldr.h>
#endif

// Needlessly cunning definitions to save memory. Why can't unions actually work? 
#define iChunkInfo (*(TChunkKernelInfo*)iInfoBuf)
#define iCodeSegInfo (*(TCodeSegKernelInfo*)iInfoBuf)
#define iThreadInfo (*(TThreadKernelInfo*)iInfoBuf)

enum TMemAreaType
	{
	EChunkArea,
	EChunkCommittedArea,
	ECodesegArea,
	EGlobalCodeArea,
	EStackArea,
	ERomArea,
	};

class TMemArea
	{
public:
	TMemArea() : iAddress(0), iSize(0) {}

	TLinAddr iAddress;
	TUint iSize;
	TMemAreaType iType;
	TFullName iName;
	};

CCommandBase* CCmdMemmap::NewLC()
	{
	CCmdMemmap* self = new(ELeave) CCmdMemmap();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdMemmap::~CCmdMemmap()
	{
	delete iMatch;
	iAddressesBuf.Close();
	}

CCmdMemmap::CCmdMemmap()
	{
	}

const TDesC& CCmdMemmap::Name() const
	{
	_LIT(KName, "memmap");	
	return KName;
	}

void CCmdMemmap::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendUintL(iPid, _L("process_id"));
	}

void CCmdMemmap::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendStringL(iMatch, _L("match"));
	aOptions.AppendBoolL(iDisplaySize, _L("size"));
	aOptions.AppendBoolL(iHumanReadableSizes, _L("human"));
	aOptions.AppendBoolL(iShowCommitted, _L("committed"));
	}

static void ReleaseCodesegMutex(TAny* aMemAccess)
	{
	static_cast<RMemoryAccess*>(aMemAccess)->ReleaseCodeSegMutex();
	}

void CCmdMemmap::DoRunL()
	{
	if (iHumanReadableSizes) iDisplaySize = ETrue;

	LoadMemoryAccessL();
	iAddressesBuf.CreateL(1024);
	if (!iMatch && iPid == 0)
		{
		// No arguments or specifying a pid of zero means get info for kernel (including user threads' kernel stacks etc)
		ShowMapForProcessL(0, iFullName);
		}
	else if (iPid)
		{
		RProcess proc;
		LeaveIfErr(proc.Open(iPid, EOwnerThread), _L("Couldn't open process of id %u"), iPid);
		iFullName = proc.FullName();
		proc.Close();
		ShowMapForProcessL(iPid, iFullName);
		}

	if (iMatch)
		{
		iFindProc.Find(*iMatch);
		while (iFindProc.Next(iFullName) == KErrNone)
			{
			RProcess proc;
			TInt err = proc.Open(iFullName, EOwnerThread);
			if (err) continue;
			if (proc.ExitType() != EExitPending)
				{
				// No point trying to read mem for a process that has exited
				proc.Close();
				continue;
				}
			TUint pid = TUint(proc.Id());
			proc.Close();
			ShowMapForProcessL(pid, iFullName);
			}
		}
	}

void CCmdMemmap::ShowMapForProcessL(TUint aPid, TFullName& aProcessName)
	{
	if (aPid) Printf(_L("\r\nProcess id %d %S:\r\n"), aPid, &aProcessName);
	RPointerArray<TMemArea> areas;
	LtkUtils::CleanupResetAndDestroyPushL(areas);
	iAddressesBuf.Zero();

	// Get all the chunks mapped into this process
	// This won't work for kernel process as it doesn't use user handles
	if (aPid)
		{
		TInt err = KErrNone;
		do
			{
			err = iMemAccess.GetAllChunksInProcess(aPid, iAddressesBuf);
			if (err == KErrOverflow)
				{
				iAddressesBuf.ReAllocL(iAddressesBuf.MaxLength() * 2);
				}
			}
		while (err == KErrOverflow);
		LeaveIfErr(err, _L("Unable to read chunk addresses for process %S"), &aProcessName);

		TInt count = iAddressesBuf.Length() / (sizeof(void*) + sizeof(TLinAddr));
		areas.ReserveL(count);
		for (TInt i = 0; i < count; i++)
			{
			TLinAddr address = ((const TLinAddr*)iAddressesBuf.Ptr())[i*2 + 1];
			TLinAddr chunkObj = ((TLinAddr*)iAddressesBuf.Ptr())[i*2];
			if (chunkObj == 0) continue; // Messy API, it can do this
			// Allow repeats in case of multiple zero addresses, but warn
			if (address == 0)
				{
				PrintWarning(_L("Chunk with null base address detected - do you need to enable FSHELL_FLEXIBLEMM_AWARE?"));
				}
			TPckg<TChunkKernelInfo> chunkInfoPckg(iChunkInfo);
			err = iMemAccess.GetObjectInfo(EChunk, (TUint8*)chunkObj, chunkInfoPckg);
			if (!err) AddChunkAreaL(areas, address, iChunkInfo);
			}
		}
	else
		{
		// Take a stab at what's valid kernel-side or otherwise is globally mapped
		// Currently we equate that with whatever was created from kernel side (ie whose controlling process id is 1)
		// TODO this doesn't work very well on flexible model...
		TInt err = KErrNone;
		do
			{
			err = iMemAccess.GetChunkAddresses(1, iAddressesBuf);
			if (err == KErrOverflow)
				{
				iAddressesBuf.ReAllocL(iAddressesBuf.MaxLength() * 2);
				}
			}
		while (err == KErrOverflow);
		LeaveIfErr(err, _L("Unable to read chunk addresses for process %S"), &aProcessName);

		TInt count = iAddressesBuf.Length() / sizeof(TLinAddr);
		areas.ReserveL(count);
		for (TInt i = 0; i < count; i++)
			{
			TLinAddr chunkObj = ((TLinAddr*)iAddressesBuf.Ptr())[i];
			TPckg<TChunkKernelInfo> chunkInfoPckg(iChunkInfo);
			TInt err = iMemAccess.GetObjectInfo(EChunk, (TUint8*)chunkObj, chunkInfoPckg);
			if (!err && iChunkInfo.iBase)
				{
				AddChunkAreaL(areas, (TLinAddr)iChunkInfo.iBase, iChunkInfo);
				}
			}
		}

	// Now get all the codesegs
	TInt count;
	if (aPid)
		{
		count = iMemAccess.AcquireCodeSegMutexAndFilterCodesegsForProcess(aPid);
		}
	else
		{
		count = iMemAccess.AcquireCodeSegMutex();
		}
	LeaveIfErr(count, _L("Couldn't acquire codeseg mutex"));
	CleanupStack::PushL(TCleanupItem(&ReleaseCodesegMutex, &iMemAccess));

	TPckg<TCodeSegKernelInfo> pkg(iCodeSegInfo);
	while (iMemAccess.GetNextCodeSegInfo(pkg) == KErrNone)
		{
		if (aPid == 0 && iCodeSegInfo.iFileName.Right(4).CompareC(_L8(".exe")) == 0) continue; // No point showing EXE codesegs as most of them will have the same address
		// Currently, all DLLs should still get unique addresses
		TMemArea area;
		area.iAddress = iCodeSegInfo.iRunAddress;
		area.iSize = iCodeSegInfo.iSize;
		area.iName.Copy(iCodeSegInfo.iFileName.Left(area.iName.MaxLength()));
		area.iType = ECodesegArea;
#ifdef FSHELL_FLEXIBLEMM_AWARE
		if (iCodeSegInfo.iAttr & ECodeSegAttGlobal) area.iType = EGlobalCodeArea;
#endif
		AddAreaL(areas, area);
		}
	CleanupStack::PopAndDestroy(); // ReleaseCodesegMutex

	// Do further check for global codesegs, which aren't included in FilterCodesegsForProcess
#ifdef FSHELL_FLEXIBLEMM_AWARE
	if (aPid)
		{
		count = iMemAccess.AcquireCodeSegMutex();
		LeaveIfErr(count, _L("Couldn't acquire codeseg mutex"));
		CleanupStack::PushL(TCleanupItem(&ReleaseCodesegMutex, &iMemAccess));
		while (iMemAccess.GetNextCodeSegInfo(pkg) == KErrNone)
			{
			if (iCodeSegInfo.iAttr & ECodeSegAttGlobal)
				{
				TMemArea area;
				area.iAddress = iCodeSegInfo.iRunAddress;
				area.iSize = iCodeSegInfo.iSize;
				area.iName.Copy(iCodeSegInfo.iFileName.Left(area.iName.MaxLength()));
				area.iType = EGlobalCodeArea;
				AddAreaL(areas, area);
				}
			}
		CleanupStack::PopAndDestroy(); // ReleaseCodesegMutex
		}
#endif

	// Now thread stacks
	if (aPid)
		{
		aProcessName.Append(_L("::*"));
		}
	else
		{
		// Get supervisor stacks for all threads
		aProcessName.Copy(_L("*"));
		}
	iFindThread.Find(aProcessName);
	TPckg<TThreadKernelInfo> threadPkg(iThreadInfo);
	while (iFindThread.Next(aProcessName) == KErrNone)
		{
		TInt err = iMemAccess.GetObjectInfo(EThread, aProcessName, threadPkg);
		if (!err)
			{
			TMemArea area;
#ifdef __EPOC32__ // On WINSCW supervisor mode still uses the user stack I think (at least, iSupervisorStack is always null)
			if (aPid == 0)
				{
				area.iAddress = iThreadInfo.iSupervisorStack;
				area.iSize = iThreadInfo.iSupervisorStackSize;
				}
			else
#endif
				{
				area.iAddress = iThreadInfo.iUserStackLimit;
				area.iSize = iThreadInfo.iUserStackSize;
				}
			area.iType = EStackArea;
			area.iName.Copy(aProcessName.Left(area.iName.MaxLength()));
			AddAreaL(areas, area);
			}
		}

	// Finally, Other Stuff we know about kernel (if applicable)
	if (aPid == 0)
		{
#ifdef __EPOC32__
		const TRomHeader* header = (const TRomHeader*)UserSvr::RomHeaderAddress();
		TMemArea romArea;
		romArea.iAddress = header->iRomBase;
		romArea.iSize = header->iRomSize;
		romArea.iType = ERomArea;

#ifndef FSHELL_9_1_SUPPORT
		if (header->iPageableRomStart)
			{
			romArea.iName = _L("ROM (Unpaged)");
			romArea.iSize = header->iPageableRomStart;
			AddAreaL(areas, romArea);

			romArea.iAddress = header->iRomBase + header->iPageableRomStart;
			romArea.iSize = header->iRomSize - header->iPageableRomStart;
			romArea.iName = _L("ROM (Pageable)");
			AddAreaL(areas, romArea);
			}
		else
#endif // FSHELL_9_1_SUPPORT
			{
			romArea.iName = _L("ROM");
			AddAreaL(areas, romArea);
			}
		romArea.iAddress = (TLinAddr)header;
		romArea.iSize = header->iRomHeaderSize;
		romArea.iName = _L("TRomHeader");
		AddAreaL(areas, romArea);
#endif // __EPOC32__

		}

	PrintAreasL(areas);
	CleanupStack::PopAndDestroy(&areas);
	}

TInt Compare(const TMemArea& aLeft, const TMemArea& aRight)
	{
	if (aLeft.iAddress == aRight.iAddress) return aRight.iSize - aLeft.iSize; // Bigger size comes first
	else return aLeft.iAddress - aRight.iAddress;
	}

void CCmdMemmap::AddAreaL(RPointerArray<TMemArea>& aAreas, const TMemArea& aArea)
	{
	TMemArea* area = new(ELeave) TMemArea;
	CleanupStack::PushL(area);
	*area = aArea;
	User::LeaveIfError(aAreas.InsertInOrderAllowRepeats(area, &Compare));
	CleanupStack::Pop(area);
	}

enum { EChunkDoubleEnded = 1, EChunkDisconnected = 2 };

void CCmdMemmap::AddChunkAreaL(RPointerArray<TMemArea>& aAreas, TLinAddr aAddress, const TChunkKernelInfo& aInfo)
	{
	TMemArea* area = new(ELeave) TMemArea;
	CleanupStack::PushL(area);
	area->iAddress = aAddress;
	area->iType = EChunkArea;
	area->iName.Copy(aInfo.iFullName);
	area->iSize = aInfo.iMaxSize;
	User::LeaveIfError(aAreas.InsertInOrderAllowRepeats(area, &Compare));
	CleanupStack::Pop(area);

	if (iShowCommitted)
		{
		TMemArea committedArea;
		committedArea.iType = EChunkCommittedArea;
		committedArea.iName = _L("Memory committed to chunk ");
		committedArea.iName.Append(area->iName);
		if (aInfo.iAttributes & EChunkDisconnected)
			{
			// Have to scan page-by-page
			RProcess proc;
			if (aInfo.iBase && aInfo.iControllingOwnerProcessId && proc.Open(aInfo.iControllingOwnerProcessId) == KErrNone)
				{
				TThreadMemoryAccessParamsBuf params;
				params().iId = proc.Id() + 1; // Lazy lazy (don't care la la la)
				proc.Close();
				params().iAddr = aInfo.iBase;
				params().iSize = 4;
				committedArea.iAddress = 0;
				TBuf8<4> buf;
				while (params().iAddr < aInfo.iBase + aInfo.iMaxSize)
					{
					TInt err = iMemAccess.GetThreadMem(params, buf); // Don't care about the contents of mem, just use err to decide if it's committed or not
					if (err)
						{
						// Finish current committed if any
						if (committedArea.iAddress)
							{
							committedArea.iSize = (TLinAddr)params().iAddr - committedArea.iAddress;
							AddAreaL(aAreas, committedArea);
							committedArea.iAddress = 0;
							}
						}
					else
						{
						if (committedArea.iAddress)
							{
							// Just carry on extending current region
							}
						else
							{
							// Start it from here
							committedArea.iAddress = (TLinAddr)params().iAddr;
							}
						}
					params().iAddr += 4096;
					}

				if (committedArea.iAddress)
					{
					// Finish final region
					committedArea.iSize = (TLinAddr)params().iAddr - committedArea.iAddress;
					AddAreaL(aAreas, committedArea);
					}
				}
			}
		else if (aInfo.iAttributes & EChunkDoubleEnded)
			{
			committedArea.iAddress = aInfo.iBottom;
			committedArea.iSize = aInfo.iSize;
			AddAreaL(aAreas, committedArea);
			}
		else
			{
			committedArea.iAddress = aAddress;
			committedArea.iSize = aInfo.iSize;
			AddAreaL(aAreas, committedArea);
			}
		}
	}


void CCmdMemmap::PrintAreasL(RPointerArray<TMemArea>& aAreas)
	{
	const TInt n = aAreas.Count();
	for (TInt i = 0; i < n; i++)
		{
		TMemArea& area = *aAreas[i];
		Printf(_L("%08x-%08x "), area.iAddress, area.iAddress + area.iSize);
		TBuf<16> sizeBuf;
		if (iDisplaySize)
			{
			if (iHumanReadableSizes)
				{
				LtkUtils::FormatSize(sizeBuf, area.iSize);
				while (sizeBuf.Length() < 10) sizeBuf.Insert(0, _L(" "));
				}
			else
				{
				sizeBuf.AppendFormat(_L("% 8d"), area.iSize);
				}
			Printf(_L("%S "), &sizeBuf);
			}
		switch(area.iType)
			{
			case EChunkArea:
				Printf(_L("[Chunk]  "));
				break;
			case EChunkCommittedArea:
				Printf(_L("         "));
				break;
			case ECodesegArea:
				Printf(_L("[Code]   "));
				break;
			case EGlobalCodeArea:
				Printf(_L("[Global] "));
				break;
			case EStackArea:
				Printf(_L("[Stack]  "));
				break;
			case ERomArea:
				Printf(_L("[ROM]    "));
				break;
			default:
				break;
			}
		Printf(_L("%S\r\n"), &area.iName);
		}
	}
