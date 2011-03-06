// ost.cpp
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
#if FSHELL_OST_SUPPORT == 2 // BC breakage...
#include <usbostcomm.h>
#else
#include <dbgtrccomm.h>
typedef RDbgTrcComm RUsbOstComm;
#endif
#include <ostprotdefs.h>
#include <fshell/ltkutils.h>

using namespace IoUtils;

class CCmdOst : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdOst();
private:
	CCmdOst();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	enum TOperation
		{
		ESend,
		EReceive,
		ESendReceive,
		};
	TOperation iOperation;
	TOstProtIds iChannelId; // See TOstProtIds in ostprotdefs.h

	RUsbOstComm iOstServer; // Cli-srv connection to usbostrouter
	RBuf8 iBuf;
	};

EXE_BOILER_PLATE(CCmdOst)

CCommandBase* CCmdOst::NewLC()
	{
	CCmdOst* self = new(ELeave) CCmdOst();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdOst::~CCmdOst()
	{
	if (iOstServer.Handle())
		{
		// RDbgTrcComm::Close isn't safe to call when not open, sigh
		iOstServer.Close();
		}
	iBuf.Close();
	}

CCmdOst::CCmdOst()
	: iChannelId(EOstProtTCFTrk)
	{
	}

const TDesC& CCmdOst::Name() const
	{
	_LIT(KName, "ost");	
	return KName;
	}

void CCmdOst::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendEnumL((TInt&)iOperation, _L("operation"));
	//TODO: Also remember to update the CIF file for any arguments you add.
	}

void CCmdOst::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendUintL((TUint&)iChannelId, _L("channel"));
	}

void CCmdOst::DoRunL()
	{
	LeaveIfErr(iOstServer.Connect(), _L("Couldn't connect to RUsbOstComm"));
	LeaveIfErr(iOstServer.Open(), _L("Couldn't open RUsbOstComm"));
	LeaveIfErr(iOstServer.RegisterProtocolID(iChannelId, EFalse), _L("Failed to register protocol id 0x%x"), iChannelId);

	if (iOperation == ESend || iOperation == ESendReceive)
		{
		TRequestStatus stat;
		iOstServer.WriteMessage(stat, _L8("PLING"));
		User::WaitForRequest(stat);
		LeaveIfErr(stat.Int(), _L("Failed to send message to RUsbOstComm"));
		}
	
	if (iOperation == EReceive || iOperation == ESendReceive)
		{
		iBuf.CreateL(4096);
		for (;;)
			{
			iBuf.Zero();
			TRequestStatus stat;
			iOstServer.ReadMessage(stat, iBuf);
			User::WaitForRequest(stat);
			LeaveIfErr(stat.Int(), _L("Failed to read message from RUsbOstComm"));
			LtkUtils::HexDumpToOutput(iBuf, Stdout());
			}
		}
	}

