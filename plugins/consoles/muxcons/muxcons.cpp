// muxcons.cpp
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

#include <e32std.h>
#include <e32cons.h>
#include <e32keys.h>
#include <fshell/consoleextensions.h>
#include "muxcons_types.h"

//#include <e32debug.h>
//#define LOG(args...) RDebug::Print(args)
#define LOG(args...)

_LIT(KDefaultMuxServ, "MuxConsSrv");

class RMuxCons : public RSessionBase
	{
public:
	TInt Connect(const TDesC& aServerName, TInt aCustomChannelId=0)
		{
		return CreateSession(aServerName, TVersion(1, 0, aCustomChannelId));
		}

	using RSessionBase::SendReceive;
	};

NONSHARABLE_CLASS(CMuxCons) : public CConsoleBase
	{
public:
	CMuxCons();
	virtual ~CMuxCons();
	virtual TInt Create(const TDesC &aTitle,TSize aSize);
	virtual void Read(TRequestStatus &aStatus);
	virtual void ReadCancel();
	virtual void Write(const TDesC &aDes);
	virtual TPoint CursorPos() const;
	virtual void SetCursorPosAbs(const TPoint &aPoint);
	virtual void SetCursorPosRel(const TPoint &aPoint);
	virtual void SetCursorHeight(TInt aPercentage);
	virtual void SetTitle(const TDesC &aTitle);
	virtual void ClearScreen();
	virtual void ClearToEndOfLine();
	virtual TSize ScreenSize() const;
	virtual TKeyCode KeyCode() const;
	virtual TUint KeyModifiers() const;
	virtual TInt Extension_(TUint aExtensionId, TAny*& a0, TAny* a1);

	void WriteStdErr(const TDesC &aDes);

private:
	RMuxCons iMux;
	TKeyCode iKeyCode;
	//TUint iModifiers;
	TPtr8 iKeyPkg;
	TInt iSpecialChannelId;
	};

CMuxCons::CMuxCons()
	: iKeyPkg((TUint8*)&iKeyCode, 4, 4)
	{
	}

// From CVtcConsoleBase::ReadKeywordValuePair
TInt ReadKeywordValuePair(TLex& aLex, TPtrC& aKeyword, TPtrC& aValue)
	{
	TLexMark mark;
	aLex.SkipSpaceAndMark(mark);
	while (!aLex.Eos() && !aLex.Peek().IsSpace() && (aLex.Peek() != '='))
		{
		aLex.Get();
		}
	aKeyword.Set(aLex.MarkedToken(mark));
	aLex.SkipSpace();
	if (aLex.Get() != '=')
		{
		return KErrArgument;
		}
	aLex.SkipSpaceAndMark(mark);
	while (!aLex.Eos() && (aLex.Peek() != ','))
		{
		aLex.Get();
		}
	aValue.Set(aLex.MarkedToken(mark));
	if (aLex.Peek() == ',')
		{
		aLex.Get();
		}
	return KErrNone;
	}

TInt CMuxCons::Create(const TDesC& aTitle, TSize /*aSize*/)
	{
	LOG(_L("Creating new mux console %S"), &aTitle);

	iSpecialChannelId = 0;
	TBuf<32> serverName;
	TPtrC title;
	TLex lex(aTitle);
	_LIT(KChannel, "channel");
	_LIT(KId, "servername");
	_LIT(KTitle, "title");
	TBool specialTitle = EFalse;
	while (!lex.Eos())
		{
		TPtrC keyword;
		TPtrC value;
		TInt err = ReadKeywordValuePair(lex, keyword, value);
		if (err) break;
		specialTitle = ETrue;
		TLex valLex(value);
		if (keyword == KChannel) valLex.Val(iSpecialChannelId);
		else if (keyword == KId) serverName.Copy(value);
		else if (keyword == KTitle) title.Set(value);
		}
	if (!specialTitle) title.Set(aTitle);

	if (serverName.Length() == 0)
		{
		// This is a bit of a hack... in the absence of any better way to figure out where to connect to, use the most recently-created server
		serverName.Copy(KDefaultMuxServ);
		TFullName found;
		TFindServer find(_L("MuxConsSrv-*"));
		while (find.Next(found) == KErrNone)
			{
			serverName.Copy(found);
			}
		}

	TInt err = iMux.Connect(serverName, iSpecialChannelId);

	if (err) return err;

	if (title.Length())
		{
		err = iMux.SendReceive(ESetTitle, TIpcArgs(&title));
		}
	return err;
	}

CMuxCons::~CMuxCons()
	{
	iMux.Close();
	}

void CMuxCons::Read(TRequestStatus &aStatus)
	{
	iMux.SendReceive(EKeypressRequest, TIpcArgs(&iKeyPkg), aStatus);
	}

void CMuxCons::ReadCancel()
	{
	iMux.SendReceive(ECancelReadData, TIpcArgs());
	}

void CMuxCons::Write(const TDesC& aDes)
	{
	iMux.SendReceive(EWriteData, TIpcArgs(&aDes));
	}

void CMuxCons::WriteStdErr(const TDesC& aDes)
	{
	//TODO this should be sent separately so we can report errors better
	iMux.SendReceive(EWriteData, TIpcArgs(&aDes));
	}

TPoint CMuxCons::CursorPos() const
	{
	TPoint result(0,0);
	TPckg<TPoint> pkg(result);
	iMux.SendReceive(ECursorPosRequest, TIpcArgs(&pkg));
	return result;
	}

void CMuxCons::SetCursorPosAbs(const TPoint& aPoint)
	{
	iMux.SendReceive(ESetAbsCursorPos, TIpcArgs(aPoint.iX, aPoint.iY));
	}

void CMuxCons::SetCursorPosRel(const TPoint& aPoint)
	{
	iMux.SendReceive(ESetRelCursorPos, TIpcArgs(aPoint.iX, aPoint.iY));
	}

void CMuxCons::SetCursorHeight(TInt aPercentage)
	{
	iMux.SendReceive(ESetCursorHeight, TIpcArgs(aPercentage));
	}

void CMuxCons::SetTitle(const TDesC& aTitle)
	{
	iMux.SendReceive(ESetTitle, TIpcArgs(&aTitle));
	}

void CMuxCons::ClearScreen()
	{
	iMux.SendReceive(EClearScreen, TIpcArgs());
	}

void CMuxCons::ClearToEndOfLine()
	{
	iMux.SendReceive(EClearToEndOfLine, TIpcArgs());
	}

TSize CMuxCons::ScreenSize() const
	{
	TSize result(80, 24);
	TPckg<TSize> pkg(result);
	iMux.SendReceive(EScreenSizeRequest, TIpcArgs(&pkg));
	LOG(_L("CMuxCons::ScreenSize() returning (%d,%d)"), result.iWidth, result.iHeight);
	return result;
	}

TKeyCode CMuxCons::KeyCode() const
	{
	return iKeyCode;
	}

TUint CMuxCons::KeyModifiers() const
	{
	return 0;
	}

extern "C" EXPORT_C TAny *NewConsole()
	{
	return(new CMuxCons);
	}

TInt CMuxCons::Extension_(TUint aExtensionId, TAny*& a0, TAny* a1)
	{
	if (aExtensionId == ConsoleStdErr::KWriteStdErrConsoleExtension)
		{
		TDesC* des = (TDesC*)a1;
		WriteStdErr(*des);
		return KErrNone;
		}
	else if (aExtensionId == ConsoleSize::KConsoleSizeReportedCorrectlyExtension)
		{
		return KErrNone;
		}
	else if (aExtensionId == ConsoleSize::KConsoleSizeNotifyChangedExtension)
		{
		TRequestStatus* stat = (TRequestStatus*)a1;
		if (stat)
			{
			iMux.SendReceive(ENotifySizeChange, TIpcArgs(), *stat);
			}
		else
			{
			iMux.SendReceive(ECancelNotifySizeChange, TIpcArgs());
			}
		return KErrNone;
		}
	else if (aExtensionId == ConsoleAttributes::KSetConsoleAttributesExtension)
		{
		ConsoleAttributes::TAttributes* attributes = (ConsoleAttributes::TAttributes*)a1;
		iMux.SendReceive(ESetAttributes, TIpcArgs(attributes->iAttributes, attributes->iForegroundColor, attributes->iBackgroundColor));
		return KErrNone;
		}
	else if (aExtensionId == ConsoleMode::KSetConsoleModeExtension)
		{
		return KErrNone; // There's nothing we need to do specially, we just have to say we allow binary mode
		}
	else if (aExtensionId == BinaryMode::KBinaryModeWriteExtension)
		{
		TDesC8* des = (TDesC8*)a1;
		return iMux.SendReceive(EWriteData8, TIpcArgs(des));
		}
	else if (aExtensionId == BinaryMode::KBinaryModeReadExtension)
		{
		// We only support binary read for nested mux sessions
		if (IS_NEST_CHANNEL(iSpecialChannelId))
			{
			TDes8* des = (TDes8*&)a0;
			TRequestStatus* stat = (TRequestStatus*)a1;
			iMux.SendReceive(EReadData8, TIpcArgs(des), *stat);
			return KErrNone;
			}
		else return KErrExtensionNotSupported;
		}
	else if (aExtensionId == DataRequester::KDataRequesterExtension)
		{
		void** args = (void**)a0;
		if (args)
			{
			const TDesC* fileName = (const TDesC*)args[0];
			const TDesC* localName = (const TDesC*)args[1];
			TRequestStatus& stat = *(TRequestStatus*)args[2];
			iMux.SendReceive(ERequestFile, TIpcArgs(fileName, localName), stat);
			}
		else
			{
			iMux.SendReceive(ECancelRequestFile);
			}
		return KErrNone;
		}
	else
		{
		return CConsoleBase::Extension_(aExtensionId, a0, a1);
		}
	}
