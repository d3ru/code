// defcons.cpp
// 
// Copyright (c) 2008 - 2010 Accenture. All rights reserved.
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
#include <e32uid.h>
#ifdef __WINS__
#include <e32wins.h>
#endif
#include <fshell/descriptorutils.h>
#include <fshell/consoleextensions.h>


//
// Constants.
//

const LtkUtils::SLitC KConsoleImplementations[] =
	{
	DESC("muxcons"),
	DESC("guicons"),
	DESC("econseik"),
	DESC("econs"),
	DESC("nullcons")
	};
const TInt KNumConsoleImplementations = sizeof(KConsoleImplementations) / sizeof(LtkUtils::SLitC);


/**

  The default console implementation, used by iosrv. This console implementation has
  one reason to exist (there used to be more):

  1) To hunt for a suitable real console implementation to be used by default. On
     GUI configurations this will either be guicons.dll or econseik.dll. On text
	 configurations this will be econs.dll.

*/
NONSHARABLE_CLASS(CDefaultConsole) : public CConsoleBase
	{
public:
	CDefaultConsole();
	virtual ~CDefaultConsole();
	virtual TInt Create(const TDesC &aTitle, TSize aSize);
	virtual void Read(TRequestStatus& aStatus);
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
private:
	TInt DoCreate();
	TInt TryCreateConsole(const TDesC& aImplementation);
private:
	HBufC* iTitle;
	TSize iSize;
	RLibrary iConsoleLibrary;
	CConsoleBase* iUnderlyingConsole;
	};

CDefaultConsole::CDefaultConsole()
	{
	}

CDefaultConsole::~CDefaultConsole()
	{
	delete iTitle;
	delete iUnderlyingConsole;
	iConsoleLibrary.Close();
	}

TInt CDefaultConsole::Create(const TDesC& aTitle, TSize aSize)
	{
	iTitle = aTitle.Alloc();
	if (iTitle)
		{
		iSize = aSize;
		return DoCreate();
		}
	return KErrNoMemory;
	}

void CDefaultConsole::Read(TRequestStatus& aStatus)
	{
	iUnderlyingConsole->Read(aStatus);
	}

void CDefaultConsole::ReadCancel()
	{
	iUnderlyingConsole->ReadCancel();
	}

void CDefaultConsole::Write(const TDesC& aDes)
	{
	iUnderlyingConsole->Write(aDes);
	}

TPoint CDefaultConsole::CursorPos() const
	{
	return iUnderlyingConsole->CursorPos();
	}

void CDefaultConsole::SetCursorPosAbs(const TPoint& aPoint)
	{
	iUnderlyingConsole->SetCursorPosAbs(aPoint);
	}

void CDefaultConsole::SetCursorPosRel(const TPoint& aPoint)
	{
	iUnderlyingConsole->SetCursorPosRel(aPoint);
	}

void CDefaultConsole::SetCursorHeight(TInt aPercentage)
	{
	iUnderlyingConsole->SetCursorHeight(aPercentage);
	}

void CDefaultConsole::SetTitle(const TDesC& aTitle)
	{
	iUnderlyingConsole->SetTitle(aTitle);
	}

void CDefaultConsole::ClearScreen()
	{
	iUnderlyingConsole->ClearScreen();
	}

void CDefaultConsole::ClearToEndOfLine()
	{
	iUnderlyingConsole->ClearToEndOfLine();
	}

TSize CDefaultConsole::ScreenSize() const
	{
	return iUnderlyingConsole->ScreenSize();
	}

TKeyCode CDefaultConsole::KeyCode() const
	{
	return iUnderlyingConsole->KeyCode();
	}

TUint CDefaultConsole::KeyModifiers() const
	{
	return iUnderlyingConsole->KeyModifiers();
	}

TInt CDefaultConsole::Extension_(TUint aExtensionId, TAny*& a0, TAny* a1)
	{
	if (iUnderlyingConsole)
		{
		return ((CBaseExtensionDummy*)iUnderlyingConsole)->Extension_(aExtensionId, a0, a1);
		}
	else
		{
		return KErrExtensionNotSupported;
		}
	}

TInt CDefaultConsole::DoCreate()
	{
	TInt err = KErrGeneral;
#ifdef __WINS__
	if (EmulatorNoGui())
		{
		err = TryCreateConsole(_L("muxcons"));
		if (err) err = TryCreateConsole(_L("nullcons"));
		}
	else if (EmulatorTextShell())
		{
		err = TryCreateConsole(_L("muxcons"));
		if (err) err = TryCreateConsole(_L("econs"));
		}
	else
#endif
		{
		for (TInt i = 0; i < KNumConsoleImplementations; ++i)
			{
			err = TryCreateConsole(KConsoleImplementations[i]);
			if (err == KErrNone)
				{
				break;
				}
			}

		}
	return err;
	}

TInt CDefaultConsole::TryCreateConsole(const TDesC& aImplementation)
	{
	TInt err = iConsoleLibrary.Load(aImplementation);
	if ((err == KErrNone) && (iConsoleLibrary.Type()[1] == KSharedLibraryUid))
		{
		TLibraryFunction entry = iConsoleLibrary.Lookup(1);
		CConsoleBase* console = (CConsoleBase*)entry();
		if (console)
			{
			err = console->Create(*iTitle, iSize);
			}
		if (err == KErrNone)
			{
			iUnderlyingConsole = console;
			}
		else
			{
			delete console;
			iConsoleLibrary.Close();
			}
		}
	return err;
	}

extern "C" EXPORT_C TAny *NewConsole()
	{
	return(new CDefaultConsole);
	}
