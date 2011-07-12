// memmap.cpp
//
// Copyright (c) 2010 Accenture. All rights reserved.
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

// Needlessly cunning definitions to save memory. Why can't unions actually work? 
#define iChunkInfo (*(TChunkKernelInfo*)iInfoBuf)
#define iCodeSegInfo (*(TCodeSegKernelInfo*)iInfoBuf)
#define iThreadInfo (*(TThreadKernelInfo*)iInfoBuf)

enum TMemAreaType
	{
	EChunkArea,
	ECodesegArea,
	EStackArea,
	ERomArea,
	};

class TMemArea
	{
public:
	TLinAddr iAddress;
	TUint iSize;
	TMemAreaType iType;
	TAny* iChunkObj;
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
			TUint pid = TUint(proc.Id());
			proc.Close();
			ShowMapForProcessL(pid, iFullName);
			}
		}
	}

void CCmdMemmap::ShowMapForProcessL(TUint aPid, TFullName& aProcessName)
	{
	if (aPid) Printf(_L("\r\nProcess id %d %S:\r\n"), aPid, &aProcessName);
	RArray<TMemArea> areas;
	CleanupClosePushL(areas);
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
			TMemArea area;
			area.iAddress = ((const TLinAddr*)iAddressesBuf.Ptr())[i*2 + 1];
			area.iSize = 0;
			area.iChunkObj = ((void**)iAddressesBuf.Ptr())[i*2];
			area.iType = EChunkArea;
			// Allow repeats in case of multiple zero addresses
			if (area.iAddress == 0)
				{
				PrintWarning(_L("Chunk with null base address detected - do you need to enable FSHELL_FLEXIBLEMM_AWARE?"));
				}
			User::LeaveIfError(areas.InsertInUnsignedKeyOrderAllowRepeats(area));
			}
		}
	else
		{
		// Take a stab at what's valid kernel-side or otherwise is globally mapped
		// Currently we equate that with whatever was created from kernel side (ie whose controlling process id is 1)
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
			TMemArea area;
			area.iChunkObj = ((void**)iAddressesBuf.Ptr())[i];
			area.iType = EChunkArea;
			TPckg<TChunkKernelInfo> chunkInfoPckg(iChunkInfo);
			TInt err = iMemAccess.GetObjectInfo(EChunk, (TUint8*)area.iChunkObj, chunkInfoPckg);
			if (!err && iChunkInfo.iBase)
				{
				area.iAddress = (TLinAddr)iChunkInfo.iBase;
				area.iName.Copy(iChunkInfo.iFullName);
				area.iSize = iChunkInfo.iSize;
				User::LeaveIfError(areas.InsertInUnsignedKeyOrder(area));
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
		area.iChunkObj = NULL;
		User::LeaveIfError(areas.InsertInUnsignedKeyOrder(area));
		}
	CleanupStack::PopAndDestroy(); // ReleaseCodesegMutex

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
			area.iChunkObj = NULL;
			User::LeaveIfError(areas.InsertInUnsignedKeyOrderAllowRepeats(area)); // TOMSCI allow temp...
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

		if (header->iPageableRomStart)
			{
			romArea.iName = _L("ROM (Unpaged)");
			romArea.iSize = header->iPageableRomStart;
			User::LeaveIfError(areas.InsertInUnsignedKeyOrder(romArea));

			romArea.iAddress = header->iRomBase + header->iPageableRomStart;
			romArea.iSize = header->iRomSize - header->iPageableRomStart;
			romArea.iName = _L("ROM (Pageable)");
			User::LeaveIfError(areas.InsertInUnsignedKeyOrder(romArea));
			}
		else
			{
			romArea.iName = _L("ROM");
			User::LeaveIfError(areas.InsertInUnsignedKeyOrder(romArea));
			}
#endif

		}

	PrintAreasL(areas);
	CleanupStack::PopAndDestroy(&areas);
	}

void CCmdMemmap::PrintAreasL(RArray<TMemArea>& aAreas)
	{
	const TInt n = aAreas.Count();
	for (TInt i = 0; i < n; i++)
		{
		TMemArea& area = aAreas[i];
		if (area.iType == EChunkArea && area.iSize == 0) // Avoid calling GetObjectInfo twice if we've already got all the info
			{
			TPckg<TChunkKernelInfo> chunkInfoPckg(iChunkInfo);
			TInt err = iMemAccess.GetObjectInfo(EChunk, (TUint8*)area.iChunkObj, chunkInfoPckg);
			if (err && err != KErrNotFound)
				{
				// Only abort on something other than KErrNotFound. KErrNotFound could legitimately
				// happen if the chunk in question has been deleted since we called RMemoryAccess::GetAllChunksInProcess.
				LeaveIfErr(err, _L("Unable to get info for chunk 0x%08x"), area.iChunkObj);
				}
			area.iName.Copy(iChunkInfo.iFullName);
			area.iSize = iChunkInfo.iSize;
			}

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
				Printf(_L("[Chunk] "));
				break;
			case ECodesegArea:
				Printf(_L("[Code]  "));
				break;
			case EStackArea:
				Printf(_L("[Stack] "));
				break;
			case ERomArea:
				Printf(_L("[ROM]   "));
				break;
			}
		Printf(_L("%S\r\n"), &area.iName);
		}
	}
