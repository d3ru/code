// ffstraceplugin.cpp
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
#include <e32property.h>
#include <f32plugin.h>
#include <f32pluginutils.h>
#include <fshell/common.mmh>
#include <fshell/ltkutils.h>
#include <fshell/stringhash.h>
using namespace ExtraBTrace;

//#define LOG_FUNC() RDebug::Printf(__PRETTY_FUNCTION__)
//#define LOG(args...) RDebug::Printf(args)
//#define LOGW(args...) RDebug::Print(args)
#define LOG_FUNC()
#define LOG(args...)
#define LOGW(args...)

NONSHARABLE_CLASS(CFfsTracerPluginFactory) : public CFsPluginFactory
	{
public:
	static CFfsTracerPluginFactory* New();

	// from CFsPluginFactory
	virtual TInt Install();
	virtual CFsPlugin* NewPluginL();
	virtual TInt UniquePosition();

private:
	CFfsTracerPluginFactory();
	};

NONSHARABLE_CLASS(CFfsTracerFsPlugin) : public CFsPlugin
	{
public:
	static CFfsTracerFsPlugin* NewL();
	virtual ~CFfsTracerFsPlugin();
	
	// from CFsPlugin
	virtual void InitialiseL();
	TInt SessionDisconnect(CSessionFs* aSession);
	virtual TInt DoRequestL(TFsPluginRequest& aRequest);
	virtual CFsPluginConn* NewPluginConnL();
private:
	CFfsTracerFsPlugin();
	void TraceThreadHandleBuf(TUint8 aSubcat, TUint aThreadId, TInt aHandle, TDes8& aBuf);
	void TraceThreadBuf(TUint8 aSubcat, TUint aThreadId, TDes8& aBuf);
	void TracePrime(TInt aHandle, TDes8& aBuf);

private:
	TBool iUseOstFormat;
	RFastLock iLock; // Protects iHandles
	LtkUtils::RStringHash<TInt> iHandles;
	TInt iNextHandleIdx;
	};
	
class CFfsTracerFsPluginConn : public CFsPluginConn
	{
public:
	CFfsTracerFsPluginConn();

	// from CFsPluginConn
	virtual TInt DoControl(CFsPluginConnRequest& aRequest);
	virtual void DoRequest(CFsPluginConnRequest& aRequest);
	virtual void DoCancel(TInt aReqMask);
	};

extern "C" EXPORT_C CFsPluginFactory* CreateFileSystem()
	{
	return CFfsTracerPluginFactory::New();
	}

CFfsTracerPluginFactory* CFfsTracerPluginFactory::New()
	{
	return new CFfsTracerPluginFactory();
	}

CFfsTracerPluginFactory::CFfsTracerPluginFactory()
	{
	}

_LIT(KFfsTracerPluginName, "FfsTracePlugin");

TInt CFfsTracerPluginFactory::Install()
	{
	LOG_FUNC();
	iSupportedDrives = 0x7FFFFFF; // KPluginSupportAllDrives | KPluginVersionTwo. Not specified symbolically to be compatible with fileservers that didn't support v2 plugins
	
	TInt err = SetName(&KFfsTracerPluginName);
	//LOG("Cat=%d, install returned %d", EFfsTrace, err);
	return err;
	}

CFsPlugin* CFfsTracerPluginFactory::NewPluginL()
	{
	return CFfsTracerFsPlugin::NewL();
	}
	
TInt CFfsTracerPluginFactory::UniquePosition()
	{
	return 0x20286F6B; // The bottom 28 bits are from FSHELL_UID_FFSTRACERPLUGIN, the top must be 2 in order to be within EPluginObserverRange
	}

//

CFfsTracerFsPluginConn::CFfsTracerFsPluginConn()
	{
	}

TInt CFfsTracerFsPluginConn::DoControl(CFsPluginConnRequest& /*aRequest*/)
	{
	return KErrNone;
	}

void CFfsTracerFsPluginConn::DoRequest(CFsPluginConnRequest& /*aRequest*/)
	{
	}

void CFfsTracerFsPluginConn::DoCancel(TInt /*aReqMask*/)
	{
	}

//

CFfsTracerFsPlugin* CFfsTracerFsPlugin::NewL()
	{
	return new(ELeave) CFfsTracerFsPlugin();
	}
	
CFfsTracerFsPlugin::CFfsTracerFsPlugin()
	: iNextHandleIdx(1)
	{
	LOG_FUNC();
	RProperty::Get(TUid::Uid(FSHELL_UID_FFSTRACE), 0, iUseOstFormat);
	LOG("iUseOstFormat = %d", iUseOstFormat);
	}

static const TFsMessage KMessages[] =
	{
	EFsDelete,
	EFsRename,
	//EFsReplace,
	EFsEntry,
	EFsFileSubClose,
	EFsFileOpen,
	EFsFileCreate,
	EFsFileReplace,
	EFsFileTemp,
	EFsFileRename,
	EFsFileRead,
	EFsFileWrite,
	};
static const TInt KMessageCount = sizeof(KMessages) / sizeof(TFsMessage);

CFfsTracerFsPlugin::~CFfsTracerFsPlugin()
	{
	LOG_FUNC();
	for (TInt i = 0; i < KMessageCount; i++)
		{
		UnregisterIntercept(KMessages[i], CFsPlugin::EPrePostIntercept);
		}

	iLock.Close();
	}

void CFfsTracerFsPlugin::InitialiseL()
	{
	User::LeaveIfError(iLock.CreateLocal());

	// intercept all calls at start
	for (TInt i = 0; i < KMessageCount; i++)
		{
		RegisterIntercept(KMessages[i], CFsPlugin::EPrePostIntercept);
		}
	}

TInt CFfsTracerFsPlugin::SessionDisconnect(CSessionFs* /*aSession*/)
	{
	// CSessionFs is a private class! It's in the main file server and 
	// has no exported methods!  What's the use of passing it here?
	return KErrNone;
	}

CFsPluginConn* CFfsTracerFsPlugin::NewPluginConnL()
	{
	LOG_FUNC();
	return new(ELeave) CFfsTracerFsPluginConn();
	}

TBool IsHandleOperation(TInt aFn)
	{
	return aFn == EFsFileSubClose || (aFn >= EFsFileOpen && aFn <= EFsFileRename);
	}

TInt CFfsTracerFsPlugin::DoRequestL(TFsPluginRequest& aRequest)
	{
	// This is where it all happens
	TInt fn = aRequest.Function();
	TBuf<128+1+256+6> name; // For renames we need it to (temporarily) fit an 8-bit filename (ie 128 wide chars) plus a full-fat TFileName. Also up to 12 bytes for threadId, handle and traceName
	GetName(&aRequest, name);
	LOGW(_L("DoRequestL fn=%d name=%S"), fn, &name);
	TInt handle = 0; // Means invalid

	TPtr8 buf8(NULL, 0);
	if (IsHandleOperation(fn))
		{
		iLock.Wait();
		TInt* handlePtr = iHandles.Find(name);
		if (!handlePtr)
			{
			TInt err = iHandles.Insert(name, iNextHandleIdx);
			if (err)
				{
				iLock.Signal();
				return KErrNone; // Don't think we should signal the error
				}
			handlePtr = iHandles.Find(name);
			iNextHandleIdx++;
			if (fn != EFsFileOpen && fn != EFsFileCreate && fn != EFfsFileReplace && fn != EFfsFileTemp)
				{
				buf8.Set(name.Collapse());
				TracePrime(*handlePtr, buf8);
				}
			}
		handle = *handlePtr;
		iLock.Signal();
		}

	// Only try to use request.Message() if it's a valid handle
	TUint threadId = 0xFFFFFFFFu;
	if(aRequest.Message().Handle() != KNullHandle)
		{
		RThread clientThread;
		TInt err = aRequest.Message().Client(clientThread, EOwnerThread);
		if (!err)
			{
			threadId = clientThread.Id();
			clientThread.Close();
			}
		}

	if (buf8.Ptr() == NULL) buf8.Set(name.Collapse()); // We're not bothered about what happens to non-ascii filenames
	TInt subcat = 0;
	if (aRequest.IsPostOperation()) subcat = EFfsPost;
	switch (fn)
		{
		case EFsDelete:
			subcat |= EFfsDelete;
			TraceThreadBuf(subcat, threadId, buf8);
			break;
		case EFsRename:
			{
			subcat |= EFfsRename;
			buf8.Append(TChar(0));
			if (buf8.Length() & 1) buf8.Append(TChar(0)); // Make sure newName will be pointing to a 16-bit-aligned location
			TPtr newName((TUint16*)(buf8.Ptr() + buf8.Length()), 0, (buf8.MaxLength() - buf8.Length())/2);
			GetNewName(&aRequest, newName);
			LOGW(_L("new name is %S"), &newName);
			newName.Collapse();
			buf8.SetLength(buf8.Length() + newName.Length());
			TraceThreadBuf(subcat, threadId, buf8);
			break;
			}
		case EFsEntry:
			subcat |= EFfsEntry;
			TraceThreadBuf(subcat, threadId, buf8);
			break;
		case EFsFileSubClose:
			subcat |= EFfsFileSubClose;
			//BTrace8(EFfsTrace, subcat, threadId, handle);
			buf8.Zero();
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileOpen:
			subcat |= EFfsFileOpen;
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileCreate:
			subcat |= EFfsFileCreate;
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileReplace:
			subcat |= EFfsFileReplace;
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileTemp:
			subcat |= EFfsFileTemp;
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileRename:
			subcat |= EFfsFileRename;
			name.Zero();
			GetNewName(&aRequest, name);
			buf8.Set(name.Collapse());
			TraceThreadHandleBuf(subcat, threadId, handle, buf8);
			break;
		case EFsFileRead:
			{
			subcat |= EFfsFileRead;
			//struct { TInt64 pos; TUint len; } data;
			buf8.SetLength(0);
			TInt64 pos;
			TUint len;
			TInt err = aRequest.Read(TFsPluginRequest::EPosition, pos);
			if (!err) err = aRequest.Read(TFsPluginRequest::ELength, len);
			if (!err)
				{
				buf8.Append((TUint8*)&pos, 8);
				buf8.Append((TUint8*)&len, 4);
				TraceThreadHandleBuf(subcat, threadId, handle, buf8);
				}
			break;
			}
		case EFsFileWrite:
			{
			subcat |= EFfsFileWrite;
			//struct { TInt64 pos; TUint len; } data;
			buf8.SetLength(0);
			TInt64 pos;
			TUint len;
			TInt err = aRequest.Read(TFsPluginRequest::EPosition, pos);
			if (!err) err = aRequest.Read(TFsPluginRequest::ELength, len);
			if (!err)
				{
				buf8.Append((TUint8*)&pos, 8);
				buf8.Append((TUint8*)&len, 4);
				TraceThreadHandleBuf(subcat, threadId, handle, buf8);
				}
			break;
			}
		default:
			// An operation we're not interested in
			break;
		}
	return KErrNone;
	}


#define TRACE_NORMAL ((TUint8)134)
#define TRACE_NAME(subcat) ((((TUint32)TRACE_NORMAL)<<16) | subcat)

void CFfsTracerFsPlugin::TraceThreadHandleBuf(TUint8 aSubcat, TUint aThreadId, TInt aHandle, TDes8& aBuf)
	{
	if (iUseOstFormat)
		{
		TUint32 traceName = TRACE_NAME(aSubcat);
		aBuf.Insert(0, TPtrC8((TUint8*)&traceName, 4));
		aBuf.Insert(4, TPtrC8((TUint8*)&aThreadId, 4));
		aBuf.Insert(8, TPtrC8((TUint8*)&aHandle, 4));
		iLock.Wait(); // Make sure we hold lock while outputting big traces involving handles to avoid another event in another thread with the same handle from being interleaved
		BTraceFilteredBig(TRACE_NORMAL, 0, FSHELL_UID_FFSTRACERPLUGIN, aBuf.Ptr(), aBuf.Size());
		iLock.Signal();
		}
	else
		{
		if (aBuf.Size() == 0)
			{
			BTrace8(EFfsTrace, aSubcat, aThreadId, aHandle);
			}
		else
			{
			aBuf.Insert(0, TPtrC8((TUint8*)&aHandle, 4));
			iLock.Wait(); // Make sure we hold lock while outputting big traces involving handles to avoid another event in another thread with the same handle from being interleaved
			BTraceBig(EFfsTrace, aSubcat, aThreadId, aBuf.Ptr(), aBuf.Size());
			iLock.Signal();
			}
		}
	}

void CFfsTracerFsPlugin::TraceThreadBuf(TUint8 aSubcat, TUint aThreadId, TDes8& aBuf)
	{
	if (iUseOstFormat)
		{
		TUint32 traceName = TRACE_NAME(aSubcat);
		aBuf.Insert(0, TPtrC8((TUint8*)&traceName, 4));
		aBuf.Insert(4, TPtrC8((TUint8*)&aThreadId, 4));
		BTraceFilteredBig(TRACE_NORMAL, 0, FSHELL_UID_FFSTRACERPLUGIN, aBuf.Ptr(), aBuf.Size());
		}
	else
		{
		BTraceBig(EFfsTrace, aSubcat, aThreadId, aBuf.Ptr(), aBuf.Size());
		}
	}


void CFfsTracerFsPlugin::TracePrime(TInt aHandle, TDes8& aBuf)
	{
	// Lock already held
	if (iUseOstFormat)
		{
		TUint32 traceName = TRACE_NAME(EFfsFilePriming);
		aBuf.Insert(0, TPtrC8((TUint8*)&traceName, 4));
		aBuf.Insert(4, TPtrC8((TUint8*)&aHandle, 4));
		BTraceFilteredBig(TRACE_NORMAL, 0, FSHELL_UID_FFSTRACERPLUGIN, aBuf.Ptr(), aBuf.Size());
		}
	else
		{
		BTraceBig(EFfsTrace, EFfsFilePriming, aHandle, aBuf.Ptr(), aBuf.Size());
		}
	}
