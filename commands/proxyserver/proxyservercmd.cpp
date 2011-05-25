// proxyservercmd.cpp
// 
// Copyright (c) 2011 Accenture. All rights reserved.
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
#include <fshell/ltkutils.h>
#include "ProxyServer.h"

using namespace IoUtils;

class CCmdProxyServer : public CCommandBase, public MMessageHandler, public MCommandExtensionsV2
	{
public:
	static CCommandBase* NewLC();
	~CCmdProxyServer();
private:
	CCmdProxyServer();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private: // From MMessageHandler
	virtual TBool HandleMessageL(TInt aMessageId, const RMessage2& aMessage);
	virtual void ForwardingMessage(const RMessage2& aMessage, TInt aMessageId, const TIpcArgs& aArgs);
	virtual void CompletingMessage(const RMessage2& aMessage, TInt aMessageId, const TIpcArgs& aArgs, TInt aCompletionCode);
private: // From MCommandExtensionsV2
	virtual void CtrlCPressed();
private:
	HBufC* iServerName;
	TBool iVerbose;

	CProxyServer* iProxy;
	};

EXE_BOILER_PLATE(CCmdProxyServer)

CCommandBase* CCmdProxyServer::NewLC()
	{
	CCmdProxyServer* self = new(ELeave) CCmdProxyServer();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdProxyServer::~CCmdProxyServer()
	{
	delete iServerName;
	}

CCmdProxyServer::CCmdProxyServer()
: CCommandBase(ESharableIoSession|ECaptureCtrlC|EManualComplete)
	{
	SetExtension(this);
	}

const TDesC& CCmdProxyServer::Name() const
	{
	_LIT(KName, "proxyserver");	
	return KName;
	}

void CCmdProxyServer::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iServerName, _L("servername"));
	}

void CCmdProxyServer::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iVerbose, _L("verbose"));
	}

void CCmdProxyServer::DoRunL()
	{
	if (iServerName->CompareC(_L("iosrv")) == 0)
		{
		LeaveIfErr(KErrArgument, _L("Attempting to intercept iosrv will end in tears!"));
		}
	TRAPL(iProxy = CProxyServer::NewInSeparateThreadL(*iServerName, this), _L("Couldn't construct proxy server for %S"), iServerName);
	}

void CCmdProxyServer::CtrlCPressed()
	{
	Printf(_L("CTRL-C detected, cleaning up proxy server...\r\n"));
	iProxy->Destroy();
	iProxy = NULL;
	Printf(_L("Exiting...\r\n"));
	SetErrorReported(ETrue); // Supress complaints about the cancel
	Complete(KErrCancel);
	}

TBool CCmdProxyServer::HandleMessageL(TInt /*aMessageId*/, const RMessage2& /*aMessage*/)
	{
	return EFalse; // Pass thru and handle in ForwardingMessage()
	}

_LIT(KCrLf, "\r\n");

void CCmdProxyServer::ForwardingMessage(const RMessage2& aMessage, TInt aMessageId, const TIpcArgs& aArgs)
	{
	RThread client;
	TInt err = aMessage.Client(client);
	TInt id = err ? err : client.Id().Id();
	Printf(_L("%d: fn=%d from thread %d"), aMessageId, aMessage.Function(), id);
	if (iVerbose)
		{
		if (!err)
			{
			TFullName threadName = client.FullName();
			Printf(_L(" %S"), &threadName);
			}
		Write(KCrLf);
		for (TInt i = 0; i < 4; i++)
			{
			Printf(_L("    Arg[%i]: "), i);
			TInt type = (aArgs.iFlags >> 3*i) & 7;
			if (type & TIpcArgs::EFlag16Bit)
				{
				Printf(_L("%S\r\n"), aArgs.iArgs[i]);
				}
			else if (type & TIpcArgs::EFlagDes)
				{
				const TDesC8* des = (const TDesC8*)aArgs.iArgs[i];
				Printf(_L8("TDesC8 length=%d:\r\n"), des->Length());
				LtkUtils::HexDumpToOutput(*des, Stdout());
				}
			else
				{
				Printf(_L("%d (0x%08x)\r\n"), aArgs.iArgs[i], aArgs.iArgs[i]);
				}
			}
		}
	else
		{
		Write(KCrLf);
		}
	client.Close();
	}

void CCmdProxyServer::CompletingMessage(const RMessage2& aMessage, TInt aMessageId, const TIpcArgs& aArgs, TInt aCompletionCode)
	{
	Printf(_L("%d: completed with %d\r\n"), aMessageId, aCompletionCode);
	if (iVerbose)
		{
		for (TInt i = 0; i < KMaxMessageArguments; i++)
			{
			if ((aArgs.iFlags & (TIpcArgs::EFlagDes<<(i*TIpcArgs::KBitsPerType))) && !(aArgs.iFlags & (TIpcArgs::EFlagConst<<(i*TIpcArgs::KBitsPerType))))
				{
				if (aArgs.iFlags & (TIpcArgs::EFlag16Bit<<(i*TIpcArgs::KBitsPerType)))
					{
					TDes16* des = (TDes16*)aArgs.iArgs[i];
					Printf(_L("    Written back arg %d TDesC16 len=%d: %S\r\n"), i, des->Length(), des);
					}
				else
					{
					TDes8* des = (TDes8*)aArgs.iArgs[i];
					Printf(_L("    Written back arg %d TDesC8 len=%d:\r\n"), i, des->Length());
					LtkUtils::HexDumpToOutput(*des, Stdout());
					}
				}
			}
		}
	}
