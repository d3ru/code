// showdebug.cpp
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

#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#include <fshell/debugrouter.h>
#include <HAL.h>

using namespace IoUtils;

class CCmdShowDebug : public CCommandBase, public MCommandExtensionsV2
	{
public:
	static CCommandBase* NewLC();
	~CCmdShowDebug();
private:
	CCmdShowDebug();
	void Log(TUint8 aWhere, TUint32 aTickCount, TUint aThreadId, const TDesC8& aMsg);
	inline TTime TickCountToTime(TUint32 aTickCount) const;

private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
	virtual void DoCancel();
	virtual void RunL();

private: // From MCommandExtensionsV2
	virtual void CtrlCPressed();


	class CLogonCompleter : public CActive
		{
	public:
		CLogonCompleter(CCmdShowDebug* aCommand) : CActive(CActive::EPriorityStandard), iCommand(aCommand)
			{
			CActiveScheduler::Add(this);
			iCommand->iProcess.Process().Logon(iStatus);
			SetActive();
			}
		void RunL() { iCommand->Complete(iStatus.Int()); }
		void DoCancel() { iCommand->iProcess.Process().LogonCancel(iStatus); }
		~CLogonCompleter() { Cancel(); }

	private:
		CCmdShowDebug* iCommand;
		};

private:
	RCloggerDebugRouter iRouter;
	RChunk iChunk;
	TBuf8<2048> iTempBuf;
	TBuf<2048> iTempWideBuf;
	RChildProcess iProcess;
	CLogonCompleter* iCompleter;
	TInt64 iStartupTickInMicroseconds;
	TTime iTimeAtStartup;
	TInt iTickFreq;

	HBufC* iProcessName;
	HBufC* iArgs;
	RArray<TBool> iVerbose;
	TBool iFilter;
	};

EXE_BOILER_PLATE(CCmdShowDebug)

CCommandBase* CCmdShowDebug::NewLC()
	{
	CCmdShowDebug* self = new(ELeave) CCmdShowDebug();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdShowDebug::~CCmdShowDebug()
	{
	Cancel();
	if (iRouter.Handle())
		{
		iRouter.EnableDebugRouting(RCloggerDebugRouter::EDisable);
		}
	iRouter.Close();
	iChunk.Close();
	delete iCompleter;
	if (iProcess.Process().Handle() && iProcess.Process().ExitType() == EExitPending)
		{
		iProcess.Process().Kill(KErrAbort);
		}
	iProcess.Close();
	delete iProcessName;
	delete iArgs;
	}

CCmdShowDebug::CCmdShowDebug()
	: CCommandBase(EManualComplete | ECaptureCtrlC)
	{
	SetExtension(this);
	}

const TDesC& CCmdShowDebug::Name() const
	{
	_LIT(KName, "showdebug");	
	return KName;
	}

void CCmdShowDebug::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iProcessName, _L("process"));
	aArguments.AppendStringL(iArgs, _L("arguments"));
	}

void CCmdShowDebug::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iVerbose, _L("verbose"));
	aOptions.AppendBoolL(iFilter, _L("filter"));
	}

void CCmdShowDebug::DoRunL()
	{
	TInt err = RCloggerDebugRouter::LoadDriver();
	if (err != KErrAlreadyExists) LeaveIfErr(err, _L("Couldn't load clogger debug router"));
	LeaveIfErr(iRouter.Open(), _L("Couldn't open debug router"));
	LeaveIfErr(iRouter.OpenChunk(iChunk), _L("Couldn't open debug router shared chunk"));
	LeaveIfErr(iRouter.EnableDebugRouting(RCloggerDebugRouter::EEnableRouting), _L("Couldn't enable routing"));
	if (iFilter && iProcessName == NULL) LeaveIfErr(KErrArgument, _L("A process must be specified when using --filter"));

	iRouter.ReceiveData(iStatus);
	SetActive();

	if (iProcessName)
		{
		TRAPL(iProcess.CreateL(*iProcessName, iArgs ? *iArgs : KNullDesC(), IoSession(), Stdin(), Stdout(), Stderr(), Env()), _L("Failed to execute %S"), iProcessName);
		iCompleter = new(ELeave) CLogonCompleter(this);
		SetErrorReported(ETrue); // So that if iProcess completes with an error it doesn't cause a strange printout when we complete with its error code
		iProcess.Process().Resume();
		}

	if (iVerbose.Count())
		{
		// Need to do some maths to figure out how to translate tick counts to time
		TUint32 tickCount = User::NTickCount();
		iTimeAtStartup.UniversalTime();
		TInt tickPeriod;
		User::LeaveIfError(HAL::Get(HAL::ENanoTickPeriod, tickPeriod));
		iTickFreq = 1000000 / tickPeriod; // We work in frequencies because they are the round numbers when using the fast counter, and at some point we might want to again

		iStartupTickInMicroseconds = ((TInt64)tickCount * 1000000) / (TInt64)iTickFreq; // Just making damn sure we're using 64bit math
		}
	}

void CCmdShowDebug::DoCancel()
	{
	iRouter.CancelReceive();
	}

TPtrC8 Read(TDes8& aTempBuf, TPtrC8& aData, TInt aLength, TPtrC8& aOverflowData)
	{
	if (aLength <= aData.Length())
		{
		// Can read it from this buffer
		TPtrC8 res(aData.Left(aLength));
		aData.Set(aData.Mid(aLength));
		return res;
		}
	else /*if (aLength > aData.Length())*/
		{
		// Descriptor spans wrap point, so need to copy into temp buf
		aTempBuf.Copy(aData.Left(aTempBuf.MaxLength())); // If anyone's crazy enough to write a platsec diagnostic string longer than 2k, it gets truncated
		TInt overflowLen = aLength - aData.Length();
		aData.Set(aOverflowData); // Wrap aData
		aOverflowData.Set(TPtrC8());
		if (overflowLen > aData.Length())
			{
			ASSERT(EFalse); // Shouldn't happen
			// in urel, return everything we've got
			return aData;
			}
		aTempBuf.Append(aData.Left(overflowLen));
		aData.Set(aData.Mid(overflowLen));
		return TPtrC8(aTempBuf);
		}
	}

void CCmdShowDebug::RunL()
	{
	TUint chunkSize = iChunk.Size();
	const TUint KDataStartOffset = sizeof(SDebugChunkHeader);
	SDebugChunkHeader* chunkHeader = (SDebugChunkHeader*)iChunk.Base();
	TUint start = chunkHeader->iStartOffset;
	TUint end = chunkHeader->iEndOffset;
	TUint overflows = chunkHeader->iOverflows;

	TBool wrap = (start > end);
	TUint endLen = wrap ? chunkSize - start : end - start;
	TUint startLen = wrap ? end - KDataStartOffset : 0;

	TPtrC8 endData(iChunk.Base() + start, endLen);
	TPtrC8 startData;
	if (wrap) startData.Set(iChunk.Base() + KDataStartOffset, startLen);
	TPtrC8 data(endData);

	while (data.Length())
		{
		TPtrC8 header = Read(iTempBuf, data, sizeof(SCloggerTraceInfo), startData);
		if (header.Length() < (TInt)sizeof(SCloggerTraceInfo))
			{
			ASSERT(EFalse); // for udeb
			break; // Something's broken
			}
		SCloggerTraceInfo info;
		Mem::Copy(&info, header.Ptr(), sizeof(SCloggerTraceInfo));
		ASSERT(info.iTraceType == 'K' || info.iTraceType == 'U' || info.iTraceType == 'P');
		TPtrC8 msg = Read(iTempBuf, data, info.iLength, startData);
		Log(info.iTraceType, info.iTickCount, info.iThreadId, msg);
		}
	if (overflows)
		{
		_LIT(KErr, "RDebug::Print buffer overflowed, %u calls not logged");
		PrintWarning(KErr, overflows);
		}

	iRouter.ReceiveData(iStatus);
	SetActive();
	}

void CCmdShowDebug::Log(TUint8 /*aWhere*/, TUint32 aTickCount, TUint aThreadId, const TDesC8& aMsg)
	{
	if (iFilter && aThreadId < iProcess.Process().Id().Id())
		{
		// A thread ID less than the process ID can never belong to that process
		return;
		}

	RThread thread; thread.SetHandle(0);
	if (iVerbose.Count() > 1 || iFilter)
		{
		// Need to open the thread in those cases
		thread.Open(aThreadId);
		}

	if (iFilter && thread.Handle())
		{
		RProcess proc;
		TInt err = thread.Process(proc);
		if (!err)
			{
			if (proc.Id() != iProcess.Process().Id())
				{
				// Trace definitely doesn't belong to our process, skip it
				proc.Close();
				thread.Close();
				return;
				}
			}
		}
	
	if (iVerbose.Count())
		{
		TDateTime dt = TickCountToTime(aTickCount).DateTime();
		_LIT(KFormat, "%i-%02i-%02i %02i:%02i:%02i.%03i: ");
		// Have to add 1 to Month and Day, as these are zero-based
		iTempWideBuf.Format(KFormat, dt.Year(), dt.Month()+1, dt.Day()+1, dt.Hour(), dt.Minute(), dt.Second(), dt.MicroSecond()/1000);
		if (iVerbose.Count() > 1 && thread.Handle())
			{
			TFullName name = thread.FullName();
			iTempWideBuf.AppendFormat(_L("%S "), &name);
			}
		else
			{
			// Just use thread id
			iTempWideBuf.AppendFormat(_L("[%d] "), aThreadId);
			}
		Write(iTempWideBuf);
		}

	thread.Close();
	
	iTempWideBuf.Copy(aMsg);
	Write(iTempWideBuf);
	Write(_L("\r\n"));
	}

void CCmdShowDebug::CtrlCPressed()
	{
	// TODO clean up iProcess

	Printf(_L("CTRL-C received, exiting.\r\n"));
	Complete();
	}

inline TTime CCmdShowDebug::TickCountToTime(TUint32 aTickCount) const
	{
	return TTime(iTimeAtStartup.Int64() + (((TInt64)aTickCount*1000000) / (TInt64)iTickFreq) - iStartupTickInMicroseconds);
	}
