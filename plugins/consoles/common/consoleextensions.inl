// consoleextensions.inl
// 
// Copyright (c) 2009 - 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

// Dummy class used to allow calls into protected Extension_ in a CBase derived class.
class CBaseExtensionDummy : public CBase
	{
public:
	using CBase::Extension_;
	// Workaround for weird RVCT compiler error in defcons when trying to use Extension_ directly
	inline TInt CallExtension_(TInt aArg, TAny*& a1, TAny* a2)
		{
		return Extension_(aArg, a1, a2);
		}
	};

TInt ConsoleMode::Set(CBase* aConsole, ConsoleMode::TMode aMode)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KSetConsoleModeExtension, ignore, reinterpret_cast<TAny*>(aMode));
	}

TInt UnderlyingConsole::Set(CBase* aConsole, CConsoleBase* aUnderlyingConsole)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KSetUnderlyingConsoleExtension, ignore, aUnderlyingConsole);
	}

ConsoleAttributes::TAttributes::TAttributes(TUint aAttributes, TColor aForegroundColor, TColor aBackgroundColor)
	: iAttributes(aAttributes), iForegroundColor(aForegroundColor), iBackgroundColor(aBackgroundColor)
	{
	}

TInt ConsoleAttributes::Set(CBase* aConsole, const TAttributes& aAttributes)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KSetConsoleAttributesExtension, ignore, (TAny*)&aAttributes);
	}

TBool LazyConsole::IsLazy(CBase* aConsole)
	{
	TBool constructed = EFalse;
	TAny* ignore;
	TInt err = ((CBaseExtensionDummy*)aConsole)->Extension_(KLazyConsoleExtension, ignore, (TAny*)&constructed);
	return (err==KErrNone);
	}

TBool LazyConsole::IsConstructed(CBase* aConsole)
	{
	TBool constructed = EFalse;
	TAny* ignore;
	TInt err = ((CBaseExtensionDummy*)aConsole)->Extension_(KLazyConsoleExtension, ignore, (TAny*)&constructed);
	return (err==KErrNone) && (constructed);
	}

TInt ConsoleStdErr::Write(CBase* aConsole, const TDesC& aDes)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KWriteStdErrConsoleExtension, ignore, (TAny*)&aDes);
	}

TBool ConsoleSize::ReportedCorrectly(CBase* aConsole)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KConsoleSizeReportedCorrectlyExtension, ignore, NULL) == KErrNone;
	}

void ConsoleSize::NotifySizeChanged(CBase* aConsole, TRequestStatus& aStatus)
	{
	TAny* ignore;
	TBool supported = ((CBaseExtensionDummy*)aConsole)->Extension_(KConsoleSizeNotifyChangedExtension, ignore, (TAny*)&aStatus) == KErrNone;
	if (!supported)
		{
		TRequestStatus* stat = &aStatus;
		User::RequestComplete(stat, KErrExtensionNotSupported);
		}
	}

void ConsoleSize::CancelNotifySizeChanged(CBase* aConsole)
	{
	TAny* ignore;
	((CBaseExtensionDummy*)aConsole)->Extension_(KConsoleSizeNotifyChangedExtension, ignore, NULL);
	}

TInt BinaryMode::Write(CBase* aConsole, const TDesC8& aBuf)
	{
	TAny* ignore;
	return ((CBaseExtensionDummy*)aConsole)->Extension_(KBinaryModeWriteExtension, ignore, (TAny*)&aBuf);
	}

TBool BinaryMode::Read(CBase* aConsole, TDes8& aBuf, TRequestStatus& aStatus)
	{
	TAny* buf = &aBuf;
	TInt result = ((CBaseExtensionDummy*)aConsole)->Extension_(KBinaryModeReadExtension, buf, (TAny*)&aStatus);
	ASSERT(result == KErrNone || result == KErrExtensionNotSupported);
	return (result == KErrNone);
	}

void DataRequester::RequestFile(CBase* aConsole, const TDesC& aFileName, const TDesC& aLocalPath, TRequestStatus& aStatus)
	{
	TAny* args[3] = { (TAny*)&aFileName, (TAny*)&aLocalPath, (TAny*)&aStatus };
	TAny* arg1 = &args[0];

	TInt ret = ((CBaseExtensionDummy*)aConsole)->Extension_(KDataRequesterExtension, arg1, NULL);
	if (ret)
		{
		TRequestStatus* stat = &aStatus;
		User::RequestComplete(stat, ret);
		}
	}


void DataRequester::CancelRequest(CBase* aConsole)
	{
	TAny* nullArg = NULL;
	((CBaseExtensionDummy*)aConsole)->Extension_(KDataRequesterExtension, nullArg, NULL);
	}