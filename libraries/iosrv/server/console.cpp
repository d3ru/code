// console.cpp
// 
// Copyright (c) 2006 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <e32cons.h>
#include <e32uid.h>
#include "server.h"
#include "console.h"
#include "log.h"


#ifdef IOSRV_LOGGING
#define CONSOLE_NAME TName consoleName(Name())
#define READER_NAME(x) TName readerName((x).IorName())
#define WRITER_NAME(x) TName writerName((x).IowName())
#else
#define CONSOLE_NAME
#define READER_NAME(x)
#define WRITER_NAME(x)
#endif

const TInt KConsoleThreadMinHeapSize = 0x1000;
const TInt KConsoleThreadMaxHeapSize = 0x1000000;

CIoConsole* CIoConsole::NewLC(const TDesC& aImplementation, const TDesC& aTitle, const TSize& aSize, const TIoConfig& aConfig, CIoConsole* aUnderlying, TUint aOptions)
	{
	CIoConsole* self = new(ELeave) CIoConsole(aConfig);
	CleanupClosePushL(*self);
	self->ConstructL(aImplementation, aTitle, aSize, aUnderlying, aOptions);
	return self;
	}

CIoConsole::~CIoConsole()
	{
	// if the sub-thread is servicing a request of ours now, we have to kill it. Otherwise, we might hang
	// when we try and cancel the request AO, if (for example) it's servicing a long-standing Create request.
	if (iRequestAo && (iRequestAo->IsActive()) && (iServerThread.Handle()!=KNullHandle))
		{
		iServerThread.Kill(KErrCancel);
		iThreadServer.Close(); // this won't be closed by the sub-thread, as we just killed it.
		}
	delete iThreadWatcher;
	delete iRequestAo;
	iRequestQueue.Close();
	delete iImplementation;
	delete iReader;
	delete iConsoleSizeChangedNotifier;
	iConsole.Close();
	delete iCreationTitle;
	iServerThread.Close();
	delete iRequestFileMessageCompleter;
	}

const TDesC& CIoConsole::Implementation() const
	{
	return *iImplementation;
	}

void CIoConsole::SessionClosed(const CIoSession& aSession)
	{
	if (&aSession == iRequestingSession && iRequestFileMessageCompleter)
		{
		iRequestFileMessageCompleter->CancelRequest();
		iRequestingSession = NULL;
		}
	}

void CIoConsole::RequestFileL(CIoSession* aRequestingSession, const RMessage2& aMessage)
	{
	if (iRequestFileMessageCompleter == NULL)
		{
		iRequestFileMessageCompleter = new(ELeave) CRequestFileMessageCompleter(this);
		}
	if (iRequestFileMessageCompleter->IsActive())
		{
		PanicClient(aMessage, EPanicRequestFileAlreadyPending);
		return;
		}
	iRequestFileMessageCompleter->RequestFileL(const_cast<RMessage2&>(aMessage));
	iRequestingSession = aRequestingSession;
	}

void CIoConsole::CancelRequestFile(const RMessage2& aMessage)
	{
	if (iRequestFileMessageCompleter)
		{
		iRequestFileMessageCompleter->CancelRequest();
		}
	iRequestingSession = NULL;
	aMessage.Complete(KErrNone);
	}

TBool CIoConsole::IsType(RIoHandle::TType aType) const
	{
	return ((aType == RIoHandle::EEndPoint) || (aType == RIoHandle::EConsole));
	}

void CIoConsole::HandleReaderDetached(MIoReader& aReader)
	{
	HandleReadWriterDetached(aReader);
	}

void CIoConsole::HandleWriterDetached(MIoWriter& aWriter)
	{
	HandleReadWriterDetached(aWriter);
	}

template <class T> void CIoConsole::HandleReadWriterDetached(T& aReadWriter)
	{
	// Remove pending requests originated from this reader / writer.
	for (TInt i = (iRequestQueue.Count() - 1); i >= 0; --i)
		{
		TConsoleRequest* request = iRequestQueue[i];
		if (request->OriginatedFrom(aReadWriter))
			{
			delete request;
			iRequestQueue.Remove(i);
			}
		}

	// If this reader / writer originated the request that's currently being process, abort it.
	if (iRequestAo->CurrentRequest() && iRequestAo->CurrentRequest()->OriginatedFrom(aReadWriter))
		{
		iRequestAo->Abort();
		}
	}

void CIoConsole::IorepReadL(MIoReader& aReader)
	{
	TDes8* binaryReadBuf = aReader.IorReadBuf8();
	if (binaryReadBuf)
		{
		// aReader better well be the foreground reader
		ASSERT(&aReader == AttachedReader());
		if (&aReader != AttachedReader()) User::Leave(KErrNotReady);

		if (!iReader->IsActive())
			{
			iReader->QueueRead(binaryReadBuf);
			}
		}
	else
		{
		QueueReaderIfRequired();
		}
	}

void CIoConsole::IorepReadKeyL(MIoReader& /*aReader*/)
	{
	QueueReaderIfRequired();
	}

void CIoConsole::IorepSetConsoleModeL(RIoReadWriteHandle::TMode aMode, MIoReader& aReader)
	{
	NewRequest(new(ELeave)TConsoleSetModeRequest(aReader, *this, aMode));
	}

void CIoConsole::IowepWriteL(MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleWriteRequest(aWriter));
	}

void CIoConsole::IowepWriteCancel(MIoWriter&)
	{
	}

void CIoConsole::IowepCursorPosL(MIoWriter& aWriter) const
	{
	NewRequest(new(ELeave)TConsoleCursorPosRequest(aWriter));
	}

void CIoConsole::IowepSetCursorPosAbsL(const TPoint& aPoint, MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleSetCursorPosAbsRequest(aWriter, aPoint));
	}

void CIoConsole::IowepSetCursorPosRelL(const TPoint& aPoint, MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleSetCursorPosRelRequest(aWriter, aPoint));
	}

void CIoConsole::IowepSetCursorHeightL(TInt aPercentage, MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleSetCursorHeightRequest(aWriter, aPercentage));
	}

void CIoConsole::IowepSetTitleL(MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleSetTitleRequest(aWriter));
	}

void CIoConsole::IowepClearScreenL(MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleClearScreenRequest(aWriter));
	}

void CIoConsole::IowepClearToEndOfLineL(MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleClearToEndOfLineRequest(aWriter));
	}

void CIoConsole::IowepSetAttributesL(TUint aAttributes, ConsoleAttributes::TColor aForegroundColor, ConsoleAttributes::TColor aBackgroundColor, MIoWriter& aWriter)
	{
	NewRequest(new(ELeave)TConsoleSetAttributesRequest(aWriter, aAttributes, aForegroundColor, aBackgroundColor));
	}

void CIoConsole::IowepScreenSizeL(MIoWriter& aWriter) const
	{
	NewRequest(new(ELeave)TConsoleScreenSizeRequest(aWriter, iConfig));
	}

CIoConsole::CIoConsole(const TIoConfig& aConfig)
	: iConfig(aConfig), iDetectedSize(-1, -1)
	{
	}

void CIoConsole::ConstructL(const TDesC& aImplementation, const TDesC& aTitle, const TSize& aSize, CIoConsole* aUnderlying, TUint aOptions)
	{
	LOG(CIoLog::Printf(_L("Console 0x%08x created"), this));
	iCreationTitle = aTitle.AllocL();
	iCreationSize = aSize;
	
	iRequestAo = new(ELeave)CConsoleRequest(*this);
	
	if (aImplementation.Length())
		{
		iImplementation = aImplementation.AllocL();
		}
	else
		{
		iImplementation = iConfig.ConsoleImplementation().AllocL();
		}
		
	User::LeaveIfError(iConsole.Connect(CIoConsoleProxyServerNewL, iImplementation, *iImplementation, KDefaultStackSize, KConsoleThreadMinHeapSize, KConsoleThreadMaxHeapSize, iThreadServer, iServerThread));
	iThreadWatcher = new(ELeave)CServerDeathWatcher(iThreadServer, iServerThread);
	if (aOptions & RIoConsole::ELazyCreate)
		{
		User::LeaveIfError(iConsole.SetLazyConstruct());
		}
	if (aUnderlying)
		{
		User::LeaveIfError(aUnderlying->Open());
		CleanupClosePushL(*aUnderlying);
		NewRequest(new(ELeave)TConsoleSetUnderlyingRequest(*aUnderlying));
		CleanupStack::Pop();
		}
	NewRequest(new(ELeave)TConsoleCreateRequest(*this));
		
	iReader = CConsoleReader::NewL(*this);
	iConsoleSizeChangedNotifier = new(ELeave) CConsoleSizeChangedNotifier(*this);
	}
	
void CIoConsole::CreateComplete(TInt aError)
	{
	iCreateStatus = aError;
	}
	
void CIoConsole::NewRequest(TConsoleRequest* aRequest) const
	{
	ASSERT(aRequest);
	TInt err = iRequestQueue.Append(aRequest);
	if (err!=KErrNone)
		{
		aRequest->CompleteD(err);
		return;
		}

	CheckQueue();
	}
	
void CIoConsole::CheckQueue() const
	{
	if (iCreateStatus != KErrNone)
		{
		while (iRequestQueue.Count())
			{
			TConsoleRequest* req = iRequestQueue[0];
			iRequestQueue.Remove(0);
			req->CompleteD(iCreateStatus);
			}
		return;
		}
	if ((!iRequestAo->IsActive()) && (iRequestQueue.Count()))
		{
		iRequestAo->Service(iRequestQueue[0]);
		iRequestQueue.Remove(0);
		}
	}
	
void CIoConsole::ConsoleDied()
	{
	iCreateStatus = KErrGeneral;
	}

void CIoConsole::ReadComplete(TInt aError)
	{
	MIoReader* foregroundReader = AttachedReader(0);
	if (foregroundReader)
		{
		foregroundReader->IorReadKeyComplete(aError, 0, 0);
		}
	}

void CIoConsole::ReadComplete(TUint aKeyCode, TUint aModifiers)
	{
	TInt index = 0;
	MIoReader* foregroundReader = AttachedReader(index++);
	MIoReader* reader = foregroundReader;
	TBool keyHandled(EFalse);
	while (reader)
		{
		if (reader->IorIsKeyCaptured(aKeyCode, aModifiers))
			{
			reader->IorReadKeyComplete(KErrNone, aKeyCode, aModifiers);
			keyHandled = ETrue;
			break;
			}
		reader = AttachedReader(index++);;
		}

	// Key not captured, so send to foreground (i.e. the first) reader.
	if (!keyHandled && foregroundReader)
		{
		foregroundReader->IorReadKeyComplete(KErrNone, aKeyCode, aModifiers);
		}

	QueueReaderIfRequired();
	}

void CIoConsole::ReadComplete(TDes8& aBinaryRead)
	{
	TPtrC8 result(aBinaryRead); // Because calling IorReadBuf8 again nukes the length of aBinaryRead (not supposed to call it again without having called IorDataBuffered first to finalise your use of it)
	MIoReader* foregroundReader = AttachedReader();
	// TODO we should take steps to ensure the read was cancelled if the reader changed while in binary mode...
	ASSERT(foregroundReader->IorReadBuf8() != NULL);
	foregroundReader->IorDataBuffered(result.Length());
	// No point calling QueueReaderIfRequired, we don't worry about multiple readers during binary read
	}

void CIoConsole::QueueReaderIfRequired()
	{
	TBool pendingReader(EFalse);
	TInt index = 0;
	MIoReader* reader = AttachedReader(index++);
	TBool foregroundReader(ETrue);
	while (reader)
		{
		if (reader->IorReadPending() || reader->IorReadKeyPending())
			{
			pendingReader = ETrue;
			break;
			}
		if (foregroundReader && reader->IorAllKeysCaptured())
			{
			// If the foreground reader has captured all keys, we don't care about the background readers.
			break;
			}
		reader = AttachedReader(index++);
		foregroundReader = EFalse;
		}

	if (pendingReader && !iReader->IsActive())
		{
		iReader->QueueRead();
		}
	else if (!pendingReader && iReader->IsActive())
		{
		iReader->Cancel();
		}
	}

CIoConsole::CConsoleReader* CIoConsole::CConsoleReader::NewL(CIoConsole& aConsole)
	{
	return new(ELeave) CConsoleReader(aConsole);
	}

CIoConsole::CConsoleReader::~CConsoleReader()
	{
	Cancel();
	}

void CIoConsole::CConsoleReader::QueueRead(TDes8* aBinaryReadBuffer)
	{
	iBinaryReadBuffer = aBinaryReadBuffer;
	if (aBinaryReadBuffer)
		{
		iConsole.iConsole.Read8(*iBinaryReadBuffer, iStatus);
		}
	else
		{
		iConsole.iConsole.Read(iKeyCodePckg, iKeyModifiersPckg, iStatus);
		}
	SetActive();
	}

CIoConsole::CConsoleReader::CConsoleReader(CIoConsole& aConsole)
	: CActive(CActive::EPriorityStandard), iConsole(aConsole)
	, iKeyCodePckg(iKeyCode), iKeyModifiersPckg(iKeyModifiers)
	{
	CActiveScheduler::Add(this);
	}

void CIoConsole::CConsoleReader::RunL()
	{
	TInt err = iStatus.Int();
	if (err==KErrServerTerminated)
		{
		iConsole.ConsoleDied();
		err = KErrGeneral;
		}
	if (err == KErrExtensionNotSupported && iBinaryReadBuffer)
		{
		// Console doesn't support it, revert to boring mode
		// This only works because in boring mode we only ever call IorReadKeyComplete() and not IorDataBuffered(), because the latter wouldn't be able to distinguish (currently) that we'd reverted to 16-bit mode
		iBinaryReadBuffer = NULL;
		iConsole.iConsole.Read(iKeyCodePckg, iKeyModifiersPckg, iStatus);
		SetActive();
		}
	else if (err)
		{
		iConsole.ReadComplete(err);
		}
	else
		{
		if (iBinaryReadBuffer)
			{
			iConsole.ReadComplete(*iBinaryReadBuffer);
			}
		else
			{
			iConsole.ReadComplete(iKeyCode, iKeyModifiers);
			}
		}
	}

void CIoConsole::CConsoleReader::DoCancel()
	{
	iConsole.iConsole.ReadCancel();
	}

//______________________________________________________________________________
//						TConsoleRequest
void CIoConsole::TConsoleRequest::PrepareL()
	{
	}

TBool CIoConsole::TConsoleRequest::OriginatedFrom(MIoReader&) const
	{ 
	return EFalse; 
	}

TBool CIoConsole::TConsoleRequest::OriginatedFrom(MIoWriter&) const
	{
	return EFalse;
	}

//______________________________________________________________________________
//						TConsoleWriterRequest
CIoConsole::TConsoleWriterRequest::TConsoleWriterRequest(MIoWriter& aWriter)
	: iWriter(aWriter)
	{
	}

TBool CIoConsole::TConsoleWriterRequest::OriginatedFrom(MIoWriter& aWriter) const
	{
	return (&iWriter == &aWriter);
	}

//______________________________________________________________________________
//						TConsoleReaderRequest
CIoConsole::TConsoleReaderRequest::TConsoleReaderRequest(MIoReader& aReader)
	: iReader(aReader)
	{
	}

TBool CIoConsole::TConsoleReaderRequest::OriginatedFrom(MIoReader& aReader) const
	{
	return (&iReader == &aReader);
	}

//______________________________________________________________________________
//						TConsoleCreateRequest
CIoConsole::TConsoleCreateRequest::TConsoleCreateRequest(CIoConsole& aOwner)
	: iOwner(aOwner)
	{
	}

void CIoConsole::TConsoleCreateRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.Create(*iOwner.iCreationTitle, iOwner.iCreationSize, aStatus);
	}

void CIoConsole::TConsoleCreateRequest::CompleteD(TInt aError)
	{
	iOwner.CreateComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleWriteRequest
CIoConsole::TConsoleWriteRequest::TConsoleWriteRequest(MIoWriter& aWriter)
	: TConsoleWriterRequest(aWriter), iBuf(NULL), iBuf8(NULL)
	{
	}

void CIoConsole::TConsoleWriteRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	if (iBuf8)
		{
		aProxy.Write8(*iBuf8, aStatus);
		}
	else if (iWriter.IowIsStdErr())
		{
		aProxy.WriteStdErr(*iBuf, aStatus);
		}
	else
		{
		aProxy.Write(*iBuf, aStatus);
		}
	}

void CIoConsole::TConsoleWriteRequest::PrepareL()
	{
	const TInt length = iWriter.IowWriteLength();
	

	ASSERT(iBuf == NULL);
	if (iWriter.IowNarrowWrite())
		{
		iBuf8 = HBufC8::NewL(length);
		TPtr8 ptr = iBuf8->Des();
		iWriter.IowWrite(ptr);
		}
	else
		{
		iBuf = HBufC::NewL(length);
		TPtr ptr = iBuf->Des();
		iWriter.IowWrite(ptr);
		}


	if (iWriter.IorwMode() == RIoReadWriteHandle::EText)
		{
		// Fix line endings (change LF to CRLF).
		// TODO should probably replace this with RLtkBuf::ReplaceAllL()
		RArray<TInt> indicies(5);
		CleanupClosePushL(indicies);
		_LIT(KCarriageReturn, "\r");
		for (TInt i = 0; i < length; ++i)
			{
			if ((*iBuf)[i] == '\n')
				{
				if ((i == 0) || ((*iBuf)[i - 1] != '\r'))
					{
					User::LeaveIfError(indicies.Append(i));
					}
				}
			}
		const TInt count = indicies.Count();
		TPtr bufPtr = iBuf->Des();
		if (count > 0)
			{
			if (bufPtr.MaxLength() < (length + count))
				{
				iBuf = iBuf->ReAllocL(length + count);
				bufPtr.Set(iBuf->Des());
				}
			for (TInt i = (count - 1); i >= 0; --i)
				{
				bufPtr.Insert(indicies[i], KCarriageReturn);
				}
			}
		CleanupStack::PopAndDestroy(&indicies);
		}
	}

void CIoConsole::TConsoleWriteRequest::CompleteD(TInt aError)
	{
	delete iBuf; iBuf = NULL;
	delete iBuf8; iBuf8 = NULL;
	iWriter.IowComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleCursorPosRequest
CIoConsole::TConsoleCursorPosRequest::TConsoleCursorPosRequest(MIoWriter& aWriter)
	: TConsoleWriterRequest(aWriter), iPosPckg(iPos)
	{
	}

void CIoConsole::TConsoleCursorPosRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.CursorPos(iPosPckg, aStatus);
	}

void CIoConsole::TConsoleCursorPosRequest::CompleteD(TInt aError)
	{
	iWriter.IowCursorPos(aError, iPos);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetCursorPosAbsRequest
CIoConsole::TConsoleSetCursorPosAbsRequest::TConsoleSetCursorPosAbsRequest(MIoWriter& aWriter, const TPoint& aPoint)
	: TConsoleWriterRequest(aWriter), iPoint(aPoint)
	{
	}

void CIoConsole::TConsoleSetCursorPosAbsRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetCursorPosAbs(iPoint, aStatus);	
	}

void CIoConsole::TConsoleSetCursorPosAbsRequest::CompleteD(TInt aError)
	{
	iWriter.IowSetCursorPosAbsComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetCursorPosRelRequest
CIoConsole::TConsoleSetCursorPosRelRequest::TConsoleSetCursorPosRelRequest(MIoWriter& aWriter, const TPoint& aPoint)
	: TConsoleWriterRequest(aWriter), iPoint(aPoint)
	{
	}

void CIoConsole::TConsoleSetCursorPosRelRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetCursorPosRel(iPoint, aStatus);
	}

void CIoConsole::TConsoleSetCursorPosRelRequest::CompleteD(TInt aError)
	{
	iWriter.IowSetCursorPosRelComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetCursorHeightRequest
CIoConsole::TConsoleSetCursorHeightRequest::TConsoleSetCursorHeightRequest(MIoWriter& aWriter, TInt aHeight)
	: TConsoleWriterRequest(aWriter), iHeight(aHeight)
	{
	}	

void CIoConsole::TConsoleSetCursorHeightRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetCursorHeight(iHeight, aStatus);
	}

void CIoConsole::TConsoleSetCursorHeightRequest::CompleteD(TInt aError)
	{
	iWriter.IowSetCursorHeightComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetTitleRequest
CIoConsole::TConsoleSetTitleRequest::TConsoleSetTitleRequest(MIoWriter& aWriter)
	: TConsoleWriterRequest(aWriter), iTitle(NULL)
	{
	}

void CIoConsole::TConsoleSetTitleRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetTitle(*iTitle, aStatus);
	}

void CIoConsole::TConsoleSetTitleRequest::PrepareL()
	{
	iTitle = iWriter.IowTitleLC();
	CleanupStack::Pop(iTitle);
	}

void CIoConsole::TConsoleSetTitleRequest::CompleteD(TInt aError)
	{
	delete iTitle; iTitle = NULL;
	iWriter.IowSetTitleComplete(aError);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleClearScreenRequest
CIoConsole::TConsoleClearScreenRequest::TConsoleClearScreenRequest(MIoWriter& aWriter)
	: TConsoleWriterRequest(aWriter)
	{
	}

void CIoConsole::TConsoleClearScreenRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.ClearScreen(aStatus);
	}

void CIoConsole::TConsoleClearScreenRequest::CompleteD(TInt aError)
	{
	iWriter.IowClearScreenComplete(aError);
	delete this;
	}

		
	

//______________________________________________________________________________
//						TConsoleClearToEndOfLineRequest
CIoConsole::TConsoleClearToEndOfLineRequest::TConsoleClearToEndOfLineRequest(MIoWriter& aWriter)
	: TConsoleWriterRequest(aWriter)
	{
	}

void CIoConsole::TConsoleClearToEndOfLineRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.ClearToEndOfLine(aStatus);
	}

void CIoConsole::TConsoleClearToEndOfLineRequest::CompleteD(TInt aError)
	{
	iWriter.IowClearToEndOfLineComplete(aError);
	delete this;
	}

		
	

//______________________________________________________________________________
//						TConsoleScreenSizeRequest
CIoConsole::TConsoleScreenSizeRequest::TConsoleScreenSizeRequest(MIoWriter& aWriter, const TIoConfig& aConfig)
	: TConsoleWriterRequest(aWriter), iConfig(aConfig), iSizeBuf(iSize)
	{
	}

void CIoConsole::TConsoleScreenSizeRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.GetScreenSize(iSizeBuf, aStatus);
	}

void CIoConsole::TConsoleScreenSizeRequest::CompleteD(TInt aError)
	{
	if (aError==KErrNone)
		{
		iSize.iWidth += iConfig.ConsoleSizeAdjustment().iWidth;
		iSize.iHeight += iConfig.ConsoleSizeAdjustment().iHeight;
		}
	iWriter.IowScreenSize(aError, iSize);
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetAttributesRequest
CIoConsole::TConsoleSetAttributesRequest::TConsoleSetAttributesRequest(MIoWriter& aWriter, TUint aAttributes, ConsoleAttributes::TColor aForegroundColor, ConsoleAttributes::TColor aBackgroundColor)
	: TConsoleWriterRequest(aWriter), iAttributes(aAttributes), iForegroundColor(aForegroundColor), iBackgroundColor(aBackgroundColor)
	{
	}

void CIoConsole::TConsoleSetAttributesRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetAttributes(iAttributes, iForegroundColor, iBackgroundColor, aStatus);
	}

void CIoConsole::TConsoleSetAttributesRequest::CompleteD(TInt aError)
	{
	iWriter.IowSetAttributesComplete(aError);
	delete this;
	}

		
//______________________________________________________________________________
//						TConsoleSetUnderlyingRequest
CIoConsole::TConsoleSetUnderlyingRequest::TConsoleSetUnderlyingRequest(CIoConsole& aUnderlyingConsole)
	: iConsole(aUnderlyingConsole)
	{
	}

void CIoConsole::TConsoleSetUnderlyingRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.SetUnderlyingConsole(iSession, aStatus);
	}

void CIoConsole::TConsoleSetUnderlyingRequest::PrepareL()
	{
	if (iConsole.iThreadServer.Handle())
		{
		User::LeaveIfError(iSession.Connect(iConsole.iThreadServer));
		}
	else
		{
		User::Leave(KErrBadHandle);
		}	
	}

void CIoConsole::TConsoleSetUnderlyingRequest::CompleteD(TInt)
	{
	iSession.Close();
	iConsole.Close();
	delete this;
	}

//______________________________________________________________________________
//						TConsoleSetModeRequest
CIoConsole::TConsoleSetModeRequest::TConsoleSetModeRequest(MIoReader& aReader,CIoConsole& aConsole, RIoReadWriteHandle::TMode aMode)
	: TConsoleReaderRequest(aReader), iConsole(aConsole), iMode(aMode)
	{
	}

void CIoConsole::TConsoleSetModeRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	iConsole.iReader->Cancel();
	aProxy.SetConsoleMode(iMode, aStatus);
	}

void CIoConsole::TConsoleSetModeRequest::CompleteD(TInt aError)
	{
	iConsole.QueueReaderIfRequired();
	iReader.IorSetConsoleModeComplete(aError);
	delete this;
	}


//______________________________________________________________________________
//						TCancelRequestFileRequest
void CIoConsole::TCancelRequestFileRequest::Request(RIoConsoleProxy aProxy, TRequestStatus& aStatus)
	{
	aProxy.CancelRequestFile(aStatus);
	}

void CIoConsole::TCancelRequestFileRequest::CompleteD(TInt /*aError*/)
	{
	// Everything of import should happen as a result of the original request completing
	delete this;
	}

//______________________________________________________________________________
//						CConsoleRequest
CIoConsole::CConsoleRequest::CConsoleRequest(CIoConsole& aConsole)
	: CActive(EPriorityStandard), iConsole(aConsole)
	{
	CActiveScheduler::Add(this);
	}
		
void CIoConsole::CConsoleRequest::Service(TConsoleRequest* aRequest)
	{
	ASSERT(!IsActive());
	ASSERT(!iCurrentRequest);
	TRAPD(err, aRequest->PrepareL());
	if (err!=KErrNone)
		{
		Complete(aRequest, err);
		return;
		}
	aRequest->Request(iConsole.iConsole, iStatus);
	SetActive();
	iCurrentRequest = aRequest;
	}
	
void CIoConsole::CConsoleRequest::Complete(TConsoleRequest* aRequest, TInt aError)
	{
	if (aError == KErrServerTerminated)
		{
		// console has panicked.
		iConsole.ConsoleDied();
		// don't want to send KErrServerTerminated up to our (iosrv) clients, as it will
		// make them think the iosrv has died.
		aError = KErrGeneral;
		}
	if (aRequest)
		{
		aRequest->CompleteD(aError);
		}
	iConsole.CheckQueue();
	}


CIoConsole::CConsoleRequest::~CConsoleRequest()
	{
	Cancel();
	}
	
void CIoConsole::CConsoleRequest::RunL()
	{
	TConsoleRequest* req = iCurrentRequest;
	iCurrentRequest = NULL;
	Complete(req, iStatus.Int());
	}

void CIoConsole::CConsoleRequest::DoCancel()
	{
	// request are handled synchronously on the server side, no cancelling is possible.
	}

const CIoConsole::TConsoleRequest* CIoConsole::CConsoleRequest::CurrentRequest() const
	{
	return iCurrentRequest;
	}

void CIoConsole::CConsoleRequest::Abort()
	{
	// We can't cancel a pending request (because they are handled synchronously on the server side),
	// so instead we delete and NULL the associated request object. This active object will then
	// continue to wait for the server to complete the request (as normal), but will take no further
	// action. This is used when the originating MIoReader or MIoWriter object has been detached from
	// the console and no longer exists.
	ASSERT(IsActive());
	ASSERT(iCurrentRequest);
	delete iCurrentRequest;
	iCurrentRequest = NULL;
	}


//______________________________________________________________________________
//						CServerDeathWatcher
CIoConsole::CServerDeathWatcher::CServerDeathWatcher(RServer2& aServer, RThread& aThread)
	: CActive(CActive::EPriorityStandard), iServer(aServer), iThread(aThread)
	{
	CActiveScheduler::Add(this);
	aThread.Logon(iStatus);
	SetActive();
	}
	
CIoConsole::CServerDeathWatcher::~CServerDeathWatcher()
	{
	Cancel();
	}

void CIoConsole::CServerDeathWatcher::RunL()
	{
	}

void CIoConsole::CServerDeathWatcher::DoCancel()
	{
	iThread.LogonCancel(iStatus);
	}

//______________________________________________________________________________
//						RIoConsoleProxy
TInt RIoConsoleProxy::SetLazyConstruct()
	{
	return SendReceive(ESetLazyConstruct);
	}
	
void RIoConsoleProxy::SetConsoleMode(RIoReadWriteHandle::TMode aMode, TRequestStatus& aStatus)
	{
	SendReceive(ESetConsoleMode, TIpcArgs(aMode), aStatus);
	}
	
void RIoConsoleProxy::SetUnderlyingConsole(const RIoConsoleProxy& aUnderlyingSession, TRequestStatus& aStatus)
	{
	SendReceive(ESetUnderlyingConsole, TIpcArgs(aUnderlyingSession), aStatus);
	}
	
TInt RIoConsoleProxy::OpenExisting()
	{
	return SendReceive(EOpenExistingConsole);
	}

void RIoConsoleProxy::WriteStdErr(const TDesC& aDescriptor, TRequestStatus& aStatus)
	{
	SendReceive(EWriteStdErr, TIpcArgs(&aDescriptor), aStatus);
	}

void RIoConsoleProxy::NotifySizeChanged(TRequestStatus& aStatus)
	{
	SendReceive(ENotifySizeChange, TIpcArgs(), aStatus);
	}

void RIoConsoleProxy::CancelNotifySizeChanged()
	{
	SendReceive(ECancelNotifySizeChange, TIpcArgs());
	}

void RIoConsoleProxy::Read8(TDes8& aBuf, TRequestStatus& aStatus)
	{
	SendReceive(EBinaryRead, TIpcArgs(&aBuf), aStatus);
	}

void RIoConsoleProxy::Write8(const TDesC8& aBuf, TRequestStatus& aStatus)
	{
	SendReceive(EBinaryWrite, TIpcArgs(&aBuf), aStatus);
	}

void RIoConsoleProxy::RequestFile(const TDesC& aBinaryName, const TDesC& aLocalName, TRequestStatus& aStatus)
	{
	SendReceive(ERequestFile, TIpcArgs(&aBinaryName, &aLocalName), aStatus);
	}

void RIoConsoleProxy::CancelRequestFile(TRequestStatus& aStatus)
	{
	SendReceive(ECancelRequestFile, aStatus);
	}

//______________________________________________________________________________
//						CIoConsoleProxyServer
CConsoleProxyServer* CIoConsoleProxyServerNewL(TAny* aParams)
	{
	const TDesC* dllName = (const TDesC*)aParams;
	RLibrary lib;

	User::LeaveIfError(lib.Load(*dllName));
	CleanupClosePushL(lib);
	if ((lib.Type()[1] == KSharedLibraryUid))
		{
		TConsoleCreateFunction entry = (TConsoleCreateFunction)lib.Lookup(1);
		if (!entry) User::Leave(KErrNotSupported);
		CleanupStack::Pop(&lib);
		return CIoConsoleProxyServer::NewL(entry, lib);
		}
	else
		{
		User::Leave(KErrNotSupported);
		return NULL; // ASSERT(Happy(compiler))
		}
	}
	
CIoConsoleProxyServer* CIoConsoleProxyServer::NewL(TConsoleCreateFunction aConsoleCreate, RLibrary& aConsoleLibrary)
	{
	CIoConsoleProxyServer* self = new CIoConsoleProxyServer(aConsoleCreate, aConsoleLibrary);
	if (!self)
		{
		aConsoleLibrary.Close();
		User::Leave(KErrNoMemory);
		}
	CleanupStack::PushL(self);
	self->ConstructL(KNullDesC);
	CleanupStack::Pop(self);
	return self;
	}
	
CIoConsoleProxyServer::CIoConsoleProxyServer(TConsoleCreateFunction aConsoleCreate, const RLibrary& aConsoleLibrary)
	: CConsoleProxyServer(aConsoleCreate, CActive::EPriorityStandard)
	, iConsoleLibrary(aConsoleLibrary)
	{
	}

CSession2* CIoConsoleProxyServer::NewSessionL(const TVersion&,const RMessage2&) const
	{
	return new(ELeave)CIoConsoleProxySession(iConsoleCreate);
	}
	
CIoConsoleProxyServer::~CIoConsoleProxyServer()
	{
	iConsoleLibrary.Close();
	}
	
MProxiedConsole* CIoConsoleProxyServer::TheConsole() const
	{
	return iTheConsole;
	}
	
void CIoConsoleProxyServer::SetTheConsole(MProxiedConsole* aConsole)
	{
	ASSERT(!iTheConsole);
	iTheConsole = aConsole;
	}

//______________________________________________________________________________
TSize DetectConsoleSize(CConsoleBase* aConsole)
	{
	TSize detectedSize;
	aConsole->SetCursorHeight(0);
	aConsole->ScreenSize(); // This used to be assigned to a variable, which was never used, but I'm not sure if calling ScreenSize() has side-effects so I'm leaving the call in
	aConsole->SetCursorPosAbs(TPoint(0, 0));
	_LIT(KSpace, " ");
	for (TInt x = 0; ; ++x)
		{
		aConsole->Write(KSpace);
		if (aConsole->CursorPos().iX == 0)
			{
			detectedSize.iWidth = x + 1;
			break;
			}
		}
	aConsole->SetCursorPosAbs(TPoint(0, 0));
	TInt prevYPos = 0;
	_LIT(KNewLine, "\r\n");
	for (TInt y = 0; ; ++y)
		{
		aConsole->Write(KNewLine);
		if (aConsole->CursorPos().iY == prevYPos)
			{
			detectedSize.iHeight = y;
			break;
			}
		else
			{
			prevYPos = y;
			}
		}
	aConsole->ClearScreen();
	aConsole->SetCursorHeight(20);
	return detectedSize;
	}

//______________________________________________________________________________
//						CIoConsoleProxySession
CIoConsoleProxySession::CIoConsoleProxySession(TConsoleCreateFunction aConsoleCreate)
	: CConsoleProxySession(aConsoleCreate), iFlags(ESupportsStdErr | ESupportsBinaryWrite)
	{
	// Assume ESupportsStdErr and ESupportsBinaryWrite until proven otherwise
	}

CIoConsoleProxySession::~CIoConsoleProxySession()
	{
	delete iSizeChangedMessageCompleter;
	delete iBinaryReadMessageCompleter;
	delete iDataRequesterMessageCompleter;
	delete iUnderlyingConsole;
	}

TInt DataRequesterCancelRequest(TAny* aConsole)
	{
	DataRequester::CancelRequest(static_cast<CBase*>(aConsole));
	return 0;
	}

void CIoConsoleProxySession::ServiceL(const RMessage2& aMessage)
	{
	switch (aMessage.Function())
		{
	case RIoConsoleProxy::ESetLazyConstruct:
		if (iConsole) User::Leave(KErrNotReady); // too late!
		SetFlag(ELazy, ETrue);
		aMessage.Complete(KErrNone);
		return;
	case RIoConsoleProxy::ESetConsoleMode:
		SetModeL(aMessage);
		return;
	case RIoConsoleProxy::ESetUnderlyingConsole:
		SetUnderlyingConsoleL(aMessage);
		return;
	case RIoConsoleProxy::EOpenExistingConsole:
		OpenExistingL(aMessage);
		return;	
	case RConsoleProxy::EGetScreenSize:
		if (GetFlag(EHaveDetectedSize))
			{
			DetectSizeL(aMessage);
			return;
			}
		// Otherwise drop through to CConsoleProxySession's implementation
		break;
	case RIoConsoleProxy::EWriteStdErr:
		{
		RBuf buf;
		CleanupClosePushL(buf);
		buf.CreateL(aMessage.GetDesLengthL(0));
		aMessage.ReadL(0, buf);
		TInt err = KErrNone;
		if (iFlags & ESupportsStdErr)
			{
			err = ConsoleStdErr::Write(iConsole->Console(), buf);
			if (err == KErrExtensionNotSupported)
				{
				// Clearly it doesn't support it, clear the flag so we fall back to normal write and don't bother trying again
				iFlags &= ~ESupportsStdErr;
				}
			}

		if (!(iFlags & ESupportsStdErr))
			{
			iConsole->Console()->Write(buf);
			err = KErrNone;
			}
		CleanupStack::PopAndDestroy(&buf);
		aMessage.Complete(err);
		return;
		}
	case RIoConsoleProxy::EBinaryWrite:
		{
		TDesC8* buf = (TDesC8*)aMessage.Ptr0(); // Hack to reduce the amount of buffer copying - don't copy the descriptor, just access it directly, since we know the client is in the same process as us
		TInt err = KErrNone;
		if (iFlags & ESupportsBinaryWrite)
			{
			err = BinaryMode::Write(iConsole->Console(), *buf);
			if (err == KErrExtensionNotSupported)
				{
				iFlags &= ~ESupportsBinaryWrite;
				}
			}

		if (!(iFlags & ESupportsBinaryWrite))
			{
			// We have to widen the descriptor. Can't see any nicer way of doing this
			HBufC* wide = HBufC::NewLC(buf->Length());
			wide->Des().Copy(*buf);
			iConsole->Console()->Write(*wide);
			err = KErrNone;
			CleanupStack::PopAndDestroy(wide);
			}
		aMessage.Complete(err);
		return;
		}
	case RIoConsoleProxy::ENotifySizeChange:
		{
		if (iSizeChangedMessageCompleter == NULL)
			{
			iSizeChangedMessageCompleter = new(ELeave) CSizeChangeMessageCompleter;
			if (iConsole) iSizeChangedMessageCompleter->SetConsole(iConsole->Console());
			}
		iSizeChangedMessageCompleter->NotifySizeChange(const_cast<RMessage2&>(aMessage));
		return;
		}
	case RIoConsoleProxy::ECancelNotifySizeChange:
		{
		//RDebug::Printf("case RIoConsoleProxy::ECancelNotifySizeChange ");
		if (iSizeChangedMessageCompleter)
			{
			iSizeChangedMessageCompleter->CancelNotify();
			}
		aMessage.Complete(KErrNone);
		return;
		}
	case RIoConsoleProxy::EBinaryRead:
		{
		if (iBinaryReadMessageCompleter == NULL)
			{
			ASSERT(iConsole);
			iBinaryReadMessageCompleter = new (ELeave) CBinaryReadMessageCompleter(iConsole->Console());
			}
		TDes8* des = (TDes8*)aMessage.Ptr0(); // Hack to reduce the amount of buffer copying - don't copy the descriptor, just access it directly, since we know the client is in the same process as us
		//RDebug::Printf("RIoConsoleProxy::EBinaryRead into buffer maxlen=%d", des->MaxLength());
		iBinaryReadMessageCompleter->Read(*des, const_cast<RMessage2&>(aMessage));
		return;
		}
	case RConsoleProxy::EReadCancel:
		// We have to handle this ourselves if we're doing a binary read
		if (iBinaryReadMessageCompleter && iBinaryReadMessageCompleter->IsActive())
			{
			iBinaryReadMessageCompleter->CancelRead();
			aMessage.Complete(KErrNone);
			return;
			}
		break; // Otherwise let CConsoleProxySession handle it
	case RIoConsoleProxy::ERequestFile:
		{
		if (iDataRequesterMessageCompleter == NULL)
			{
			if (!iConsole) User::Leave(KErrNotReady);
			iDataRequesterMessageCompleter = new(ELeave) CGenericMessageCompleter();
			iDataRequesterMessageCompleter->SetCancelCallback(TCallBack(&DataRequesterCancelRequest, iConsole->Console()));
			}
		const TDesC* fileName = (const TDesC*)aMessage.Ptr0();
		const TDesC* localName = (const TDesC*)aMessage.Ptr1();
		DataRequester::RequestFile(iConsole->Console(), *fileName, *localName, iDataRequesterMessageCompleter->iStatus);
		iDataRequesterMessageCompleter->SetMessageAndActive(const_cast<RMessage2&>(aMessage));
		return;
		}
	case RIoConsoleProxy::ECancelRequestFile:
		if (iDataRequesterMessageCompleter)
			{
			iDataRequesterMessageCompleter->CancelRequest();
			}
		aMessage.Complete(KErrNone);
		return;
	default:
		break;
		}

	CConsoleProxySession::ServiceL(aMessage);
	}
	
MProxiedConsole* CIoConsoleProxySession::InstantiateConsoleL()
	{
	if (Server()->TheConsole()!=NULL)
		{
		// make sure that only 1 console is ever created in this server
		User::Leave(KErrAlreadyExists);
		}
	MProxiedConsole* cons;
	if (GetFlag(ELazy))
		{
		CLazyConsole* lazy = new(ELeave) CLazyConsole(iConsoleCreate);
		CleanupStack::PushL(lazy);
		cons = MProxiedConsole::DefaultL(lazy);
		CleanupStack::Pop();
		}
	else
		{
		cons = CConsoleProxySession::InstantiateConsoleL();
		}
		
	if (iUnderlyingConsole)
		{
		TInt err = UnderlyingConsole::Set(cons->Console(), iUnderlyingConsole);
		// if this succeeds, ownership of the underlying console has been taken
		// if it didn't, we should delete it as it's not needed.
		if (err!=KErrNone)
			{
			delete iUnderlyingConsole;
			}
		iUnderlyingConsole = NULL;
		}
	
	return cons;	
	}
	
void CIoConsoleProxySession::ConsoleCreatedL(MProxiedConsole* aConsole)
	{
	Server()->SetTheConsole(aConsole);
	if (!GetFlag(ELazy))
		{
		// If it's lazy, we can't check ReportedCorrectly until it's been instantiated
		CConsoleBase* console = aConsole->Console();
		if (!ConsoleSize::ReportedCorrectly(console))
			{
			iDetectedSize = DetectConsoleSize(console);
			SetFlag(EHaveDetectedSize, ETrue);
			}
		}

	if (iSizeChangedMessageCompleter) iSizeChangedMessageCompleter->SetConsole(aConsole->Console());
	}

void CIoConsoleProxySession::DetectSizeL(const RMessage2& aMessage)
	{
	if (!iConsole) User::Leave(KErrNotReady);
	
	aMessage.WriteL(0, TPckg<TSize>(iDetectedSize));
	aMessage.Complete(KErrNone);	
	}

void CIoConsoleProxySession::SetModeL(const RMessage2& aMessage)
	{
	if (!iConsole) User::Leave(KErrNotReady);
	RIoReadWriteHandle::TMode mode = (RIoReadWriteHandle::TMode)aMessage.Int0();
	TInt err = ConsoleMode::Set(iConsole->Console(), (mode == RIoReadWriteHandle::EBinary) ? ConsoleMode::EBinary : ConsoleMode::EText);
	aMessage.Complete(err);
	}

void CIoConsoleProxySession::SetUnderlyingConsoleL(const RMessage2& aMessage)
	{
	if (iUnderlyingConsole) User::Leave(KErrAlreadyExists);
	
	RIoConsoleProxy underlyingSession;
	RThread client;
	aMessage.ClientL(client, EOwnerThread);
	CleanupClosePushL(client);

	underlyingSession.SetHandle(aMessage.Int0());
	User::LeaveIfError(underlyingSession.Duplicate(client, EOwnerThread));
	
	CleanupClosePushL(underlyingSession);
	User::LeaveIfError(underlyingSession.OpenExisting());
	
	CConsoleProxy* underlying = CWriteOnlyConsoleProxy::NewL(underlyingSession);
	
	CleanupStack::PopAndDestroy(2, &client); // we can close underlyingSession as it's been duplicated by CConsoleProxy::NewL
	
	if (iConsole && iConsole->Console())
		{
		CleanupStack::PushL(underlying);
		User::LeaveIfError(UnderlyingConsole::Set(iConsole->Console(), underlying));
		// ownership of underlying now taken.
		CleanupStack::Pop();
		}
	else
		{
		// save it for when the console is instantiated
		iUnderlyingConsole = underlying;
		}
	
	aMessage.Complete(KErrNone);
	}

void CIoConsoleProxySession::OpenExistingL(const RMessage2& aMessage)
	{
	if (Server()->TheConsole()==NULL) User::Leave(KErrNotReady); // no console to connect to
	
	iConsole = Server()->TheConsole();
	iConsole->Open();
	aMessage.Complete(KErrNone);
	}

TBool CIoConsoleProxySession::GetFlag(TFlag aFlag)
	{
	return iFlags & aFlag ? (TBool)ETrue : EFalse;
	}
	
void CIoConsoleProxySession::SetFlag(TFlag aFlag, TBool aSet)
	{
	if (aSet)
		{
		iFlags |= aFlag;
		}
	else
		{
		iFlags &= (~(TUint)aFlag);
		}
	}


//______________________________________________________________________________
//						CWriteOnlyConsoleProxy
CConsoleProxy* CWriteOnlyConsoleProxy::NewL(const RConsoleProxy& aProxySession)
	{
	CWriteOnlyConsoleProxy* self = new(ELeave)CWriteOnlyConsoleProxy();
	CleanupStack::PushL(self);
	self->ConstructL(aProxySession);
	CleanupStack::Pop(self);
	return self;
	}
	
CWriteOnlyConsoleProxy::CWriteOnlyConsoleProxy()
	{
	}

void CWriteOnlyConsoleProxy::Read(TRequestStatus&)
	{
	User::Panic(KIoServerName, EPanicCannotReadFromUnderlyingConsole);
	}

void CWriteOnlyConsoleProxy::ReadCancel()
	{
	}

TKeyCode CWriteOnlyConsoleProxy::KeyCode() const
	{
	return EKeyNull;
	}


TUint CWriteOnlyConsoleProxy::KeyModifiers() const
	{
	return 0;
	}

//______________________________________________________________________________
//						CLazyConsole
CLazyConsole::CLazyConsole(TConsoleCreateFunction aConsoleCreate)
	: iConsoleCreate(aConsoleCreate)
	{
	}

CLazyConsole::~CLazyConsole()
	{
	iTitle.Close();
	delete iConsole;
	delete iUnderlyingConsole;
	}

TInt CLazyConsole::Create(const TDesC &aTitle,TSize aSize)
	{
	iSize = aSize;
	return iTitle.Create(aTitle);
	}
	
TInt CLazyConsole::CheckCreated() const
	{
	if (iCreateError) return iCreateError;
	if (iConsole) return KErrNone;
	
	TRAP(iCreateError, iConsole = iConsoleCreate());
	if ((iCreateError==KErrNone) && (!iConsole))
		{
		iCreateError = KErrNoMemory;
		}
	if (iCreateError == KErrNone)
		{
		TName procName = RProcess().Name(); // econseik sets the process name to the console title...
		iCreateError = iConsole->Create(iTitle, iSize);
		User::RenameProcess(procName.Left(procName.Locate('['))); // ...so restore it just in case
		}
	if ((iCreateError == KErrNone) && !ConsoleSize::ReportedCorrectly(iConsole))
		{
		iDetectedSize = DetectConsoleSize(iConsole);
		iHaveDetectedSize = ETrue;
		}
	if (iCreateError == KErrNone && iStatusForNotifySizeRequest != NULL)
		{
		ConsoleSize::NotifySizeChanged(iConsole, *iStatusForNotifySizeRequest);
		iStatusForNotifySizeRequest = NULL;
		}
	if (iCreateError == KErrNone && iUnderlyingConsole != NULL)
		{
		TInt err = UnderlyingConsole::Set(iConsole, iUnderlyingConsole);
		if (err)
			{
			delete iUnderlyingConsole;
			}
		iUnderlyingConsole = NULL;
		}

	if (iCreateError != KErrNone)
		{
		delete iConsole;
		iConsole = NULL;
		}
	return iCreateError;
	}

void CLazyConsole::Read(TRequestStatus &aStatus)
	{
	TInt err = CheckCreated();
	if (err)
		{
		TRequestStatus* stat = &aStatus;
		User::RequestComplete(stat, err);
		return;
		}
	iConsole->Read(aStatus);
	}

void CLazyConsole::ReadCancel()
	{
	if (iConsole)
		{
		iConsole->ReadCancel();
		}
	}

void CLazyConsole::Write(const TDesC &aDes)
	{
	if (CheckCreated() == KErrNone)
		{
		iConsole->Write(aDes);
		}
	}

TPoint CLazyConsole::CursorPos() const
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->CursorPos();
		}
	return TPoint(0,0);
	}

void CLazyConsole::SetCursorPosAbs(const TPoint &aPoint)
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->SetCursorPosAbs(aPoint);
		}
	}

void CLazyConsole::SetCursorPosRel(const TPoint &aPoint)
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->SetCursorPosRel(aPoint);
		}
	}

void CLazyConsole::SetCursorHeight(TInt aPercentage)
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->SetCursorHeight(aPercentage);
		}
	}

void CLazyConsole::SetTitle(const TDesC &aTitle)
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->SetTitle(aTitle);
		}
	}

void CLazyConsole::ClearScreen()
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->ClearScreen();
		}
	}

void CLazyConsole::ClearToEndOfLine()
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->ClearToEndOfLine();
		}
	}

TSize CLazyConsole::ScreenSize() const
	{
	if (CheckCreated() == KErrNone)
		{
		if (iHaveDetectedSize)
			{
			return iDetectedSize;
			}
		else
			{
			return iConsole->ScreenSize();
			}
		}
	else
		{
		return TSize(0,0);
		}
	}

TKeyCode CLazyConsole::KeyCode() const
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->KeyCode();
		}
	return EKeyNull;
	}

TUint CLazyConsole::KeyModifiers() const
	{
	if (CheckCreated() == KErrNone)
		{
		return iConsole->KeyModifiers();
		}
	return 0;
	}

TInt CLazyConsole::Extension_(TUint aExtensionId, TAny*& a0, TAny* a1)
	{
	if (aExtensionId == LazyConsole::KLazyConsoleExtension)
		{
		TBool* constructed = (TBool*)a1;
		*constructed = (iConsole != NULL);
		return KErrNone;
		}
	else if (iConsole == NULL && aExtensionId == ConsoleMode::KSetConsoleModeExtension && (ConsoleMode::TMode)(TUint)a1 == ConsoleMode::EText)
		{
		// A console that isn't created yet will default to text mode anyway so we don't need to force instantiation. This works around an issue with iosrv calling ConsoleMode::Set even on the underlying console
		return KErrNone;
		}
	else if (iConsole == NULL && aExtensionId == ConsoleSize::KConsoleSizeNotifyChangedExtension)
		{
		// Remember this notify for later
		TRequestStatus* stat = (TRequestStatus*)a1;
		//RDebug::Printf("Lazycons KConsoleSizeNotifyChangedExtension a1=%x iStatusForNotifySizeRequest=%x", a1, iStatusForNotifySizeRequest);
		if (stat == NULL && iStatusForNotifySizeRequest != NULL)
			{
			User::RequestComplete(iStatusForNotifySizeRequest, KErrCancel);
			}
		iStatusForNotifySizeRequest = stat;
		return KErrNone; // It's ok to say KErrNone now but complete the TRequestStatus with KErrExtensionNotSupported later
		}
	else if (iConsole == NULL && aExtensionId == UnderlyingConsole::KSetUnderlyingConsoleExtension)
		{
		iUnderlyingConsole = (CConsoleBase*)a1;
		return KErrNone;
		}
	else 
		{
		TInt err = CheckCreated();
		if (err == KErrNone)
			{
			return ((CLazyConsole*)iConsole)->Extension_(aExtensionId, a0, a1);
			}
		return err;
		}
	}

//

CIoConsole::CConsoleSizeChangedNotifier::CConsoleSizeChangedNotifier(CIoConsole& aConsole)
	: CActive(CActive::EPriorityStandard), iConsole(aConsole)
	{
	CActiveScheduler::Add(this);
	iConsole.iConsole.NotifySizeChanged(iStatus);
	SetActive();
	}

CIoConsole::CConsoleSizeChangedNotifier::~CConsoleSizeChangedNotifier()
	{
	Cancel();
	}

void CIoConsole::CConsoleSizeChangedNotifier::RunL()
	{
	if (iStatus.Int() != KErrNone) // eg KErrExtensionNotSupported
		{
		return;
		}

	iConsole.iConsole.NotifySizeChanged(iStatus);
	SetActive();

	MIoReader* fg = iConsole.AttachedReader();
	if (fg)
		{
		fg->IorReaderChange(RIoReadHandle::EConsoleSizeChanged);
		}
	}

void CIoConsole::CConsoleSizeChangedNotifier::DoCancel()
	{
	//RDebug::Printf("Calling RIoConsoleProxt::CancelNotifySizeChanged");
	iConsole.iConsole.CancelNotifySizeChanged();
	}

//

CSizeChangeMessageCompleter::CSizeChangeMessageCompleter()
	: CActive(CActive::EPriorityStandard)
	{
	CActiveScheduler::Add(this);
	}

CSizeChangeMessageCompleter::~CSizeChangeMessageCompleter()
	{
	Cancel();
	}

void CSizeChangeMessageCompleter::NotifySizeChange(RMessagePtr2& aMessage)
	{
	iMessage = aMessage;
	if (iActualConsole)
		{
		ConsoleSize::NotifySizeChanged(iActualConsole, iStatus);
		SetActive();
		}
	}

void CSizeChangeMessageCompleter::RunL()
	{
	iMessage.Complete(iStatus.Int());
	}

void CSizeChangeMessageCompleter::DoCancel()
	{
	ASSERT(iActualConsole);
	ConsoleSize::CancelNotifySizeChanged(iActualConsole);
	}

void CSizeChangeMessageCompleter::SetConsole(CConsoleBase* aConsole)
	{
	ASSERT(iActualConsole == NULL);
	iActualConsole = aConsole;
	if (!iMessage.IsNull())
		{
		ConsoleSize::NotifySizeChanged(iActualConsole, iStatus);
		SetActive();
		}
	}

void CSizeChangeMessageCompleter::CancelNotify()
	{
	//RDebug::Printf("CSizeChangeMessageCompleter::CancelNotify");
	if (IsActive())
		{
		Cancel();
		}
	
	if (!iMessage.IsNull())
		{
		iMessage.Complete(KErrCancel);
		}
	}

//

CGenericMessageCompleter::CGenericMessageCompleter()
	: CActive(CActive::EPriorityStandard)
	{
	CActiveScheduler::Add(this);
	}

CGenericMessageCompleter::~CGenericMessageCompleter()
	{
	Cancel();
	}

void CGenericMessageCompleter::SetCancelCallback(TCallBack aCallback, TCancelType aCancelType)
	{
	iCancelType = aCancelType;
	iCancelCallback = aCallback;
	}

void CGenericMessageCompleter::SetMessageAndActive(RMessagePtr2& aMessage)
	{
	ASSERT(iMessage.IsNull());
	ASSERT(!IsActive());
	iMessage = aMessage;
	SetActive();
	}

void CGenericMessageCompleter::RunL()
	{
	if (!iMessage.IsNull())
		{
		iMessage.Complete(iStatus.Int());
		}
	}

void CGenericMessageCompleter::DoCancel()
	{
	iCancelCallback.CallBack();
	}

void CGenericMessageCompleter::CancelRequest()
	{
	//RDebug::Printf("CGenericMessageCompleter::CancelRequest");
	if (IsActive())
		{
		if (iCancelType == ESync)
			{
			Cancel();
			}
		else
			{
			iCancelCallback.CallBack();
			}
		}
	
	if (!iMessage.IsNull())
		{
		iMessage.Complete(KErrCancel);
		}
	}

//

CRequestFileMessageCompleter::CRequestFileMessageCompleter(CIoConsole* aConsole)
	: iConsole(aConsole)
	{
	SetCancelCallback(TCallBack(&CancelRequestFile, this), EAsync);
	}

TInt CRequestFileMessageCompleter::CancelRequestFile(TAny* aSelf)
	{
	CRequestFileMessageCompleter* self = static_cast<CRequestFileMessageCompleter*>(aSelf);
	CIoConsole::TCancelRequestFileRequest* req = new CIoConsole::TCancelRequestFileRequest;
	if (req)
		{
		self->iConsole->NewRequest(req);
		}
	// If we've no memory to allocated the cancel request, tough
	return 0;
	}

void CRequestFileMessageCompleter::RunL()
	{
	delete iLocalName; iLocalName = NULL;
	delete iFileName; iFileName = NULL;
	CGenericMessageCompleter::RunL();
	}

CRequestFileMessageCompleter::~CRequestFileMessageCompleter()
	{
	Cancel();
	delete iLocalName;
	delete iFileName;
	}

void CRequestFileMessageCompleter::RequestFileL(RMessagePtr2& aMessage)
	{
	ASSERT(iFileName == NULL);
	ASSERT(iLocalName == NULL);
	HBufC* fileName = HBufC::NewLC(aMessage.GetDesLengthL(0));
	TPtr fileNamePtr = fileName->Des();
	HBufC* localName = HBufC::NewLC(aMessage.GetDesLengthL(1));
	TPtr localNamePtr = localName->Des();
	aMessage.ReadL(0, fileNamePtr);
	aMessage.ReadL(1, localNamePtr);
	CleanupStack::Pop(2, fileName);
	iFileName = fileName;
	iLocalName = localName;

	iConsole->iConsole.RequestFile(*iFileName, *iLocalName, iStatus);
	SetMessageAndActive(aMessage);
	}

//

CBinaryReadMessageCompleter::CBinaryReadMessageCompleter(CConsoleBase* aConsole)
	: CActive(CActive::EPriorityStandard), iActualConsole(aConsole)
	{
	CActiveScheduler::Add(this);
	}

CBinaryReadMessageCompleter::~CBinaryReadMessageCompleter()
	{
	CancelRead();
	}

void CBinaryReadMessageCompleter::Read(TDes8& aBuf, RMessage2& aMessage)
	{
	//RDebug::Printf("CIoConsole reading max %d bytes", aBuf.MaxLength());
	TBool supported = BinaryMode::Read(iActualConsole, aBuf, iStatus);
	if (!supported)
		{
		//TODO don't know why I bothered with the TBool return...
		aMessage.Complete(KErrExtensionNotSupported);
		}
	else
		{
		iMessage = aMessage;
		SetActive();
		}
	}

void CBinaryReadMessageCompleter::CancelRead()
	{
	if (IsActive())
		{
		Cancel();
		}
	
	if (!iMessage.IsNull())
		{
		iMessage.Complete(KErrCancel);
		}
	}

void CBinaryReadMessageCompleter::DoCancel()
	{
	// We use the same cancel function as the other reads... hope that doesn't cause trouble...
	iActualConsole->ReadCancel();
	}

void CBinaryReadMessageCompleter::RunL()
	{
	//RDebug::Printf("RIoConsoleProxy::EBinaryRead completed with %d", iStatus.Int());
	iMessage.Complete(iStatus.Int());
	}
