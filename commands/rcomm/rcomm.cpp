// rcomm.cpp
// 
// Copyright (c) 2007 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <fshell/ioutils.h>
#include <c32comm.h>

using namespace IoUtils;

class CCmdRcomm : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdRcomm();
private:
	CCmdRcomm();
	void ReadL();
	void WriteL();
	void ListenForCommandsL();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	HBufC* iCsyName;
	HBufC* iPortName;
	enum
		{
		EReadAndPrintStdout,
		EWriteFromStdin,
		EReadAndConsume,
		ECommand,
		} iMode;
	TCommAccess iCommAccess;
	TUint iBufSize;
	TBool iRaw;
	TBool iVerbose;
	RCommServ iCommServ;
	RComm iComm;
	};


CCommandBase* CCmdRcomm::NewLC()
	{
	CCmdRcomm* self = new(ELeave) CCmdRcomm();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdRcomm::~CCmdRcomm()
	{
	delete iCsyName;
	delete iPortName;
	iComm.Close();
	iCommServ.Close();
	}

CCmdRcomm::CCmdRcomm()
	: iCommAccess(ECommShared), iBufSize(1024)
	{
	}

const TDesC& CCmdRcomm::Name() const
	{
	_LIT(KName, "rcomm");	
	return KName;
	}

void CCmdRcomm::DoRunL()
	{
	User::LeaveIfError(iCommServ.Connect());

	if (iCsyName)
		{
		TInt err = iCommServ.LoadCommModule(*iCsyName);
		if (err && (err != KErrAlreadyExists))
			{
			PrintError(err, _L("Unable to load comm module \'%S\'"), iCsyName);
			User::Leave(err);
			}
		}
	
	if (iPortName == NULL)
		{
		if (iCsyName == NULL)
			{
			TInt numPorts;
			User::LeaveIfError(iCommServ.NumPorts(numPorts));
			for (TInt i = 0; i < numPorts; ++i)
				{
				TName moduleName;
				TSerialInfo serialInfo;
				TInt err = iCommServ.GetPortInfo(i, moduleName, serialInfo);
				if (err == KErrNone)
					{
					Printf(_L("%S:\r\n\tPort name: \'%S\'\r\n\tLow unit: %d\r\n\tHigh unit: %d\r\n\tDescription: %S\r\n\r\n"), &moduleName, &serialInfo.iName, serialInfo.iLowUnit, serialInfo.iHighUnit, &serialInfo.iDescription);
					}
				else
					{
					PrintWarning(_L("Couldn't get info for port %d, err=%d"), i, err);
					}
				}
			}
		else
			{
			TSerialInfo serialInfo;
			_LIT(KCsy, ".csy");
			TPtrC csyName(*iCsyName);
			if (csyName.Right(KCsy().Length()).CompareF(KCsy) == 0)
				{
				// Remove the trailing '.csy' because otherwise C32 won't find the module.
				csyName.Set(csyName.Left(csyName.Length() - KCsy().Length()));
				}
			TInt err = iCommServ.GetPortInfo(csyName, serialInfo);
			LeaveIfErr(err, _L("Couldn't get port info for CSY '%S'"), &csyName);
			Printf(_L("Port name: \'%S\'\r\nLow unit: %d\r\nHigh unit: %d\r\nDescription: %S\r\n"), &serialInfo.iName, serialInfo.iLowUnit, serialInfo.iHighUnit, &serialInfo.iDescription);
			}
		return;
		}

	TInt err = iComm.Open(iCommServ, *iPortName, iCommAccess);
	LeaveIfErr(err, _L("Unable to open port \'%S\'"), iPortName);

	switch (iMode)
		{
	case EReadAndConsume:
		iRaw = ETrue;
		// Drop through
	case EReadAndPrintStdout:
		ReadL();
		break;
	case EWriteFromStdin:
		WriteL();
		break;
	case ECommand:
		ListenForCommandsL();
		break;
		}
	}

void CCmdRcomm::ReadL()
	{
	if (iRaw)
		{
		HBufC8* readBuf = HBufC8::NewLC(iBufSize);
		TPtr8 readBufPtr(readBuf->Des());
		TInt err = KErrNone;
		while (err == KErrNone)
			{
			TRequestStatus status;
			iComm.ReadOneOrMore(status, readBufPtr);
			User::WaitForRequest(status);
			err = status.Int();
			if (err == KErrNone)
				{
				if (iMode == EReadAndPrintStdout)
					{
					TPtrC writePtr((TUint16*)readBuf->Ptr(), readBuf->Length() / 2);
					Write(writePtr);
					}
				}
			else if (iVerbose)
				{
				PrintWarning(_L("RComm::ReadOneOrMore failed with %d"), err);
				}
			}
		CleanupStack::PopAndDestroy(readBuf);
		}
	else
		{
		HBufC8* readBuf = HBufC8::NewLC(iBufSize);
		TPtr8 readBufPtr(readBuf->Des());
		HBufC* writeBuf = HBufC::NewLC(iBufSize / 2);
		TPtr writeBufPtr(writeBuf->Des());
		TInt err = KErrNone;
		while (err == KErrNone)
			{
			TRequestStatus status;
			iComm.ReadOneOrMore(status, readBufPtr);
			User::WaitForRequest(status);
			err = status.Int();
			if (err == KErrNone)
				{
				writeBufPtr.Copy(*readBuf);
				Write(*writeBuf);
				}
			else if (iVerbose)
				{
				PrintWarning(_L("RComm::ReadOneOrMore failed with %d"), err);
				}
			}
		CleanupStack::PopAndDestroy(2, readBuf);
		}
	}

void CCmdRcomm::WriteL()
	{
	RIoConsoleReadHandle& stdin = Stdin();
	User::LeaveIfError(stdin.SetReadMode(RIoReadHandle::EOneOrMore));

	if (iRaw)
		{
		HBufC* readBuf = HBufC::NewLC(iBufSize / 2);
		TPtr readBufPtr(readBuf->Des());
		TInt err = KErrNone;
		while (err == KErrNone)
			{
			err = stdin.Read(readBufPtr);
			if (err == KErrNone)
				{
				TPtrC8 writePtr((TUint8*)readBuf->Ptr(), readBuf->Size());
				TRequestStatus status;
				iComm.Write(status, writePtr);
				User::WaitForRequest(status);
				LeaveIfErr(status.Int(), _L("RComm::Write failed with %d"), status.Int());
				}
			}
		CleanupStack::PopAndDestroy(readBuf);
		}
	else
		{
		HBufC* readBuf = HBufC::NewLC(iBufSize / 2);
		TPtr readBufPtr(readBuf->Des());
		HBufC8* writeBuf = HBufC8::NewLC(iBufSize);
		TPtr8 writeBufPtr(writeBuf->Des());
		TInt err = KErrNone;
		while (err == KErrNone)
			{
			err = stdin.Read(readBufPtr);
			if (err == KErrNone)
				{
				writeBufPtr.Copy(*readBuf);
				TRequestStatus status;
				iComm.Write(status, *writeBuf);
				User::WaitForRequest(status);
				LeaveIfErr(status.Int(), _L("RComm::Write failed with %d"), status.Int());
				}
			}
		CleanupStack::PopAndDestroy(2, readBuf);
		}
	}

void CCmdRcomm::ArgumentsL(RCommandArgumentList& aArguments)
	{
	_LIT(KPortName, "port_name");
	aArguments.AppendStringL(iPortName, KPortName);

	_LIT(KMode, "mode");
	aArguments.AppendEnumL((TInt&)iMode, KMode);
	}

void CCmdRcomm::OptionsL(RCommandOptionList& aOptions)
	{
	_LIT(KOption1, "access");
	aOptions.AppendUintL((TUint&)iCommAccess, KOption1);

	_LIT(KOption2, "buffer");
	aOptions.AppendUintL(iBufSize, KOption2);

	_LIT(KOption3, "raw");
	aOptions.AppendBoolL(iRaw, KOption3);

	_LIT(KOption4, "verbose");
	aOptions.AppendBoolL(iVerbose, KOption4);

	_LIT(KCsy, "csy-name");
	aOptions.AppendStringL(iCsyName, KCsy);
	}


EXE_BOILER_PLATE(CCmdRcomm)

void CCmdRcomm::ListenForCommandsL()
	{
	HBufC8* buf = HBufC8::NewMaxLC(iBufSize);
	memset((void*)buf->Ptr(), 'z', iBufSize);

	for (;;)
		{
		TRequestStatus status;
		TBuf8<1> cmdBuf;
		iComm.ReadOneOrMore(status, cmdBuf);
		User::WaitForRequest(status);
		LeaveIfErr(status.Int(), _L("Failed to read command from serial port"));

		TUint8 cmd = cmdBuf[0];
		if (cmd <= 25)
			{
			TInt n = 1 << cmd;
			while (n--)
				{
				iComm.Write(status, *buf);
				User::WaitForRequest(status);
				LeaveIfErr(status.Int(), _L("Couldn't write to serial port"));
				}
			}
		else
			{
			break;
			}
		}

	CleanupStack::PopAndDestroy(buf);
	}
