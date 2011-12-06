// ffstrace.cpp
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

#include <fshell/extrabtrace.h>
#include <fshell/btrace_parser.h>

#include <fshell/ioutils.h>
#include <fshell/common.mmh>

using namespace IoUtils;
using namespace ExtraBTrace;

class CCmdFfstrace : public CCommandBase, public MBtraceObserver
	{
public:
	static CCommandBase* NewLC();
	~CCmdFfstrace();
private:
	CCmdFfstrace();
	void HandleBtraceFrameL(const TBtraceFrame& aFrame);
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	enum TCmd
		{
		EMonitor,
		ELoad,
		EUnload,
		};
	TCmd iCommand;
	TBool iVerbose;
private:
	CBtraceReader* iBtraceReader;
	TBtraceTickCount iNowTick;
	TTime iNowTime;
	RHashMap<TInt, HBufC*> iHandles;
	};

EXE_BOILER_PLATE(CCmdFfstrace)

CCommandBase* CCmdFfstrace::NewLC()
	{
	CCmdFfstrace* self = new(ELeave) CCmdFfstrace();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdFfstrace::~CCmdFfstrace()
	{
	delete iBtraceReader;
	THashMapIter<TInt, HBufC*> iter(iHandles);
	while (iter.NextKey())
		{
		delete *iter.CurrentValue();
		}
	iHandles.Close();
	}

CCmdFfstrace::CCmdFfstrace()
	: CCommandBase(EManualComplete)
	{
	}

const TDesC& CCmdFfstrace::Name() const
	{
	_LIT(KName, "ffstrace");	
	return KName;
	}

void CCmdFfstrace::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendEnumL((TInt&)iCommand, _L("command"));
	}

void CCmdFfstrace::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iVerbose, _L("verbose"));
	}

void CCmdFfstrace::DoRunL()
	{
#ifdef FSHELL_TRACECORE_SUPPORT
	_LIT_SECURITY_POLICY_PASS(KPass);
	TInt err = RProperty::Define(0, RProperty::EInt, KPass, KPass);
	if (err != KErrAlreadyExists) LeaveIfErr(err, _L("Couldn't define property"));
	TBool useOst = (iCommand == ELoad); // Don't use OST format if we're monitoring, because CBtraceReader doesn't understand OST
	RProperty::Set(TUid::Uid(FSHELL_UID_FFSTRACE), 0, useOst);
#endif

	_LIT(KPluginDll, "ffstraceplugin.fxt");
	_LIT(KFfsTracerPluginName, "FfsTracePlugin");
	if (iCommand == EMonitor || iCommand == ELoad)
		{
		err = FsL().AddPlugin(KPluginDll);
		if (err && err != KErrAlreadyExists) LeaveIfErr(err, _L("Couldn't load filesystem plugin %S"), &KPluginDll);

		err = FsL().MountPlugin(KFfsTracerPluginName);
		//TODO handle this being called repeatedly
		LeaveIfErr(err, _L("Couldn't mount filesystem plugin %S"), &KFfsTracerPluginName);
		}
	else if (iCommand == EUnload)
		{
		FsL().DismountPlugin(KFfsTracerPluginName);
		LeaveIfErr(FsL().RemovePlugin(KFfsTracerPluginName), _L("No plugin to unload"));
		}

	if (iCommand == EMonitor)
		{
		iNowTick.SetToNow();
		iNowTime.HomeTime();
		iBtraceReader = CBtraceReader::NewL(CBtraceReader::EFlushOnBtraceThreshold, 1024*1024, 512*1024);
		iBtraceReader->AddObserverL(ExtraBTrace::EFfsTrace, *this);
		iBtraceReader->SetMultipartReassemblyL(10);
		iBtraceReader->Start(iNowTick, 1000000);
		}
	else
		{
		Complete(KErrNone);
		}
	}

void CCmdFfstrace::HandleBtraceFrameL(const TBtraceFrame& aFrame)
	{
	if (aFrame.iSubCategory == EFfsFilePriming)
		{
		TPtrC name((TUint16*)(aFrame.iData.Ptr()+4), (aFrame.iData.Size()-4)/2);
		iHandles.InsertL(*(TUint32*)aFrame.iData.Ptr(), name.AllocLC());
		CleanupStack::Pop();
		}
	else
		{
		TInt fn = aFrame.iSubCategory & ~ExtraBTrace::EFfsPost;
		TBool post = aFrame.iSubCategory & ExtraBTrace::EFfsPost;
		TUint threadId = *(TUint*)aFrame.iData.Ptr();

		// Work out name
		TPtrC name;
		TPtrC newName;
		TInt handle = 0;
		switch (fn)
			{
		// These don't use handles at all
		case EFfsDelete:
		case EFfsEntry:
			name.Set(TPtrC((TUint16*)(aFrame.iData.Ptr()+4), (aFrame.iData.Size()-4)/2));
			break;
		case EFfsRename:
			{
			name.Set(TPtrC((TUint16*)(aFrame.iData.Ptr()+4), (aFrame.iData.Size()-4)/2));
			TInt separator = name.Locate(0);
			if (separator != KErrNotFound)
				{
				newName.Set(name.Mid(separator+1));
				name.Set(name.Left(separator));
				}
			break;
			}
			
		// These have filename and update handles
		case EFfsFileOpen:
		case EFfsFileCreate:
		case EFfsFileReplace:
		case EFfsFileTemp:
		case EFfsFileRename:
			{
			handle = *(TInt*)(aFrame.iData.Ptr() + 4);
			name.Set(TPtrC((TUint16*)(aFrame.iData.Ptr()+8), (aFrame.iData.Size()-8)/2));
			HBufC** existingName = iHandles.Find(handle);
			if (existingName)
				{
				if (**existingName != name)
					{
					delete *existingName;
					iHandles.Remove(handle);
					iHandles.InsertL(handle, name.AllocLC());
					CleanupStack::Pop();
					}
				}
			else
				{
				iHandles.InsertL(handle, name.AllocLC());
				CleanupStack::Pop();
				}
			break;
			}


		// These just have handles
		case EFfsFileSubClose:
		case EFfsFileRead:
		case EFfsFileWrite:
			{
			handle = *(TInt*)(aFrame.iData.Ptr() + 4);
			HBufC** namePtr = iHandles.Find(handle);
			if (namePtr) name.Set(**namePtr);
			break;
			}

		default:
			return;
			}


		if (iVerbose)
			{
			if (aFrame.iTickCount.iNano < iNowTick.iNano)
				{
				// This shouldn't happen now I've fixed btrace_parser.dll for SMP
				PrintWarning(_L("aFrame nano=%u fast=%u < iNowTick nano=%u fast=%u"), aFrame.iTickCount.iNano, aFrame.iTickCount.iFast, iNowTick.iNano, iNowTick.iFast);
				}
			else
				{
				TDateTime dt = (iNowTime + aFrame.iTickCount.IntervalInMicroSeconds(iNowTick)).DateTime();
				_LIT(KFormat, "%i-%02i-%02i %02i:%02i:%02i.%03i: ");
				// Have to add 1 to Month and Day, as these are zero-based
				Printf(KFormat, dt.Year(), dt.Month()+1, dt.Day()+1, dt.Hour(), dt.Minute(), dt.Second(), dt.MicroSecond()/1000);
				}
			}

		// Not everything gets as far as post - if an error occurs during the pre checking for eg
		if (post) Printf(_L("-"));
		else Printf(_L("+"));

		switch (fn)
			{
			case ExtraBTrace::EFfsDelete:
				Printf(_L("Delete %S"), &name);
				break;
			case ExtraBTrace::EFfsFileOpen:
				Printf(_L("Open %S"), &name);
				break;
			case ExtraBTrace::EFfsFileCreate:
				Printf(_L("Create %S"), &name);
				break;
			case ExtraBTrace::EFfsFileSubClose:
				Printf(_L("Close %S"), &name);
				break;
			case ExtraBTrace::EFfsFileReplace:
				Printf(_L("Replace %S"), &name);
				break;
			case ExtraBTrace::EFfsFileTemp:
				Printf(_L("Open temp %S"), &name);
				break;
			case ExtraBTrace::EFfsRename:
			case ExtraBTrace::EFfsFileRename:
				Printf(_L("Rename from %S to %S"), &name, &newName);
				break;
			case ExtraBTrace::EFfsEntry:
				Printf(_L("RFs::Entry %S"), &name);
				break;
			case ExtraBTrace::EFfsFileRead:
				{
				TInt64 pos;
				memcpy(&pos, aFrame.iData.Ptr() + 8, sizeof(TInt64));
				TInt len = *(TInt*)(aFrame.iData.Ptr() + 16);
				Printf(_L("Read 0x%Ld+%d from %S"), pos, len, &name);
				break;
				}
			case ExtraBTrace::EFfsFileWrite:
				{
				TInt64 pos;
				memcpy(&pos, aFrame.iData.Ptr() + 8, sizeof(TInt64));
				TInt len = *(TInt*)(aFrame.iData.Ptr() + 16);
				Printf(_L("Write 0x%Ld+%d from %S"), pos, len, &name);
				break;
				}
			default:
				Printf(_L("Event %d"), fn);
				break;
			}
		Printf(_L(" from thread %u"), threadId);
		RThread thread;
		TInt err = thread.Open(threadId);
		if (err == KErrNone)
			{
			Printf(_L(" "));
			TFullName name = thread.FullName();
			Write(name);
			thread.Close();
			}
		Printf(_L("\r\n"));
		}
	}
