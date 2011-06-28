// consolewrapper.cpp
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

#include "consolewrapper.h"
#include "fshell_launcherview.h"
#include "utils.h"
#include <eikenv.h>
#include <aknbutton.h>
#include <fshell/common.mmh>
#include <fshell/guicons.h>

#define LCONS(x) L ## x
#define EXPAND(f, x) f(x)

_LIT(KProcess, "fshell.exe");
_LIT(KErrorFormat, "Error: %d");

static const TInt KConsoleHeight = 100;
static const TSize KCloseButtonSize(25, 25);

class CCloseButton : public CCoeControl
{
public:
	static CCloseButton* NewL(CCoeControl& aParent);
	void Draw(const TRect& aRect) const;
private:
	void ConstructL(CCoeControl& aParent);
};

CCloseButton* CCloseButton::NewL(CCoeControl& aParent)
	{
	CCloseButton* self = new (ELeave) CCloseButton();
	CleanupStack::PushL(self);
	self->ConstructL(aParent);
	CleanupStack::Pop();
	return self;
	}

void CCloseButton::ConstructL(CCoeControl& aParent)
	{
	CreateWindowL(&aParent);
	static_cast<RWindow*>(DrawableWindow())->SetTransparencyAlphaChannel();
	SetSize(KCloseButtonSize);
	MakeVisible(ETrue);
	ActivateL();
	}

void CCloseButton::Draw(const TRect& aRect) const
	{
	CWindowGc& gc = SystemGc();
	gc.SetBrushColor(TRgb(255, 0, 0, 128));
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.Clear(aRect);
	gc.SetBrushColor(TRgb(255, 255, 255, 255));
	static const TInt KCrossWidth = 3;
	static const TInt KBorderWidth = 2;
	TPoint points[8];
	points[0] = Rect().iTl + TPoint(KBorderWidth + KCrossWidth, KBorderWidth);
	points[1] = Rect().iTl + TPoint(KBorderWidth, KBorderWidth + KCrossWidth);
	points[2] = Rect().iTl + TPoint(Size().iWidth - KBorderWidth - KCrossWidth, Size().iHeight - KBorderWidth);
	points[3] = Rect().iTl + TPoint(Size().iWidth - KBorderWidth, Size().iHeight - KBorderWidth - KCrossWidth);
	points[4] = Rect().iTl + TPoint(Size().iWidth - KBorderWidth, KBorderWidth + KCrossWidth);
	points[5] = Rect().iTl + TPoint(Size().iWidth - KBorderWidth - KCrossWidth, KBorderWidth);
	points[6] = Rect().iTl + TPoint(KBorderWidth, KBorderWidth + Size().iHeight - KBorderWidth - KCrossWidth);
	points[7] = Rect().iTl + TPoint(KBorderWidth + KCrossWidth, Size().iHeight - KBorderWidth);
	gc.DrawPolygon(points, 4);
	gc.DrawPolygon(points + 4, 4);
	}

// Active Object used to delay destruction of CConsoleWrapper until the CONE
// event handler has completed
class CDelayedDestroy : public CActive
{
public:
	CDelayedDestroy(CConsoleWrapper &aParent);
	~CDelayedDestroy();
	void Trigger();
private:
	void RunL();
	void DoCancel();
private:
	CConsoleWrapper* iParent;
};

CDelayedDestroy::CDelayedDestroy(CConsoleWrapper &aParent)
:	CActive(EPriorityStandard)
,	iParent(&aParent)
	{
	CActiveScheduler::Add(this);
	}

CDelayedDestroy::~CDelayedDestroy()
	{
	Cancel();
	}

void CDelayedDestroy::Trigger()
	{
	DEBUG_PRINTF("CDelayedDestroy::Trigger 0x%08x parent 0x%08x", this, iParent);
	Cancel();
	iStatus = KRequestPending;
	SetActive();
	TRequestStatus* status = &iStatus;
	User::RequestComplete(status, KErrNone);
	}

void CDelayedDestroy::RunL()
	{
	DEBUG_PRINTF("CDelayedDestroy::RunL 0x%08x parent 0x%08x", this, iParent);
	delete iParent;
	}

void CDelayedDestroy::DoCancel()
	{

	}

class CProcessMonitor : public CActive
{
public:
	CProcessMonitor(CConsoleWrapper &aParent);
	~CProcessMonitor();
	void Logon(RProcess& aProcess);
	void Logoff();
private:
	void RunL();
	void DoCancel();
private:
	CConsoleWrapper* iParent;
	RProcess* iProcess;
};

CProcessMonitor::CProcessMonitor(CConsoleWrapper &aParent)
:	CActive(EPriorityStandard)
,	iParent(&aParent)
	{
	CActiveScheduler::Add(this);
	}

CProcessMonitor::~CProcessMonitor()
	{
	Cancel();
	}

void CProcessMonitor::Logon(RProcess& aProcess)
	{
	ASSERT(!IsActive());
	iStatus = KRequestPending;
	aProcess.Logon(iStatus);
	iProcess = &aProcess;
	SetActive();
	}

void CProcessMonitor::Logoff()
	{
	Cancel();
	}

void CProcessMonitor::RunL()
	{
	iParent->ChildProcessEnded(iStatus.Int());
	}

void CProcessMonitor::DoCancel()
	{
	iProcess->LogonCancel(iStatus);
	}

CConsoleWrapper* CConsoleWrapper::NewLC(CFShellLauncherAppView& aView)
	{
	CConsoleWrapper* self = new(ELeave) CConsoleWrapper(aView);
	CleanupStack::PushL(self);
	self->ConstructL();
	return self;
	}

CConsoleWrapper::CConsoleWrapper(CFShellLauncherAppView& aView)
:	iView(aView)
	{
	DEBUG_PRINTF("CConsoleWrapper::CConsoleWrapper 0x%08x", this);
	}

void CConsoleWrapper::ConstructL()
	{
	CreateWindowL(&iView);
	iCloseButton = CCloseButton::NewL(*this);
	iCloseButton->SetObserver(this);
	iProcessMonitor = new (ELeave) CProcessMonitor(*this);
	iDestroy = new (ELeave) CDelayedDestroy(*this);
	SetSize(TSize(0, KConsoleHeight));
	ActivateL();
	}

CConsoleWrapper::~CConsoleWrapper()
	{
	DEBUG_PRINTF("CConsoleWrapper::~CConsoleWrapper 0x%08x", this);
	delete iDestroy;
	CloseEndpoint();
	delete iProcessMonitor;
	delete iCloseButton;
	}

TInt CConsoleWrapper::CountComponentControls() const
	{
	TInt count = 1;
	if (iConsole)
		{
		++count;
		}
	return count;
	}

CCoeControl* CConsoleWrapper::ComponentControl(TInt aIndex) const
	{
	CCoeControl* result = iCloseButton;
	if (0 == aIndex && iConsole)
		{
		result = iConsole;
		}
	return result;
	}

CConsoleControl* CConsoleWrapper::Console() const
	{
	return iConsole;
	}

void CConsoleWrapper::HandleNewConsoleL(CConsoleControl* aConsole)
	{
	DEBUG_PRINTF("CConsoleWrapper::HandleNewConsoleL 0x%08x console 0x%08x", this, aConsole);
	ASSERT(!iConsole);
	if (!aConsole->DrawableWindow())
		{
		aConsole->SetContainerWindowL(*this);
		}
	iConsole = aConsole;
	Layout();
	iConsole->ActivateL();
	}

void CConsoleWrapper::HandleConsoleClosed(CConsoleControl* aConsole)
	{
	DEBUG_PRINTF("CConsoleWrapper::HandleConsoleClosed 0x%08x console 0x%08x", this, aConsole);
	ASSERT(aConsole == iConsole);
	iConsole->Write(_L("\n\n*** Console closed ***"));
	Layout();
	}

void CConsoleWrapper::DelayedDestroy()
	{
	DEBUG_PRINTF("CConsoleWrapper::DelayedDestroy 0x%08x", this);
	iDestroy->Trigger();
	}

void CConsoleWrapper::SizeChanged()
	{
    Layout();
	}

void CConsoleWrapper::Draw(const TRect& aRect) const
	{
	CWindowGc& gc = SystemGc();
	gc.SetBrushColor(TRgb(128, 128, 128));
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.Clear(aRect);
	iCloseButton->DrawDeferred();
	}

void CConsoleWrapper::HandleControlEventL(CCoeControl* aControl, TCoeEvent aEventType)
	{
	if (aControl == iCloseButton && EEventRequestFocus == aEventType)
		{
		DEBUG_PRINTF("CConsoleWrapper::HandleControlEventL 0x%08x close button pressed", this);
		CloseEndpoint();
		iView.CloseConsoleL(*this);
		}
	}

void CConsoleWrapper::Layout()
    {
	static const TInt KPadding = 5;
	iCloseButton->SetPosition(TPoint(Size().iWidth - iCloseButton->Size().iWidth - KPadding, KPadding));
	if (iConsole)
		{
		iConsole->SetExtent(TPoint(), Size());
		}
    DrawDeferred();
    }

void CConsoleWrapper::OpenEndpointL(TConnectionType aConnectionType)
	{
	DEBUG_PRINTF("CConsoleWrapper::OpenEndpointL 0x%08x type %d", this, aConnectionType);
	CloseEndpoint();
	const TDesC* args = NULL;
	TBool needsUnderlying = ETrue;
	switch (aConnectionType)
		{
		case EConnectionUsb:
#ifdef FSHELL_LAUNCHER_SUPPORT_USB
			_LIT(KArgsUsb, "--console vt100usbcons --console-title '" EXPAND(LCONS, FSHELL_LAUNCHER_SUPPORT_USB) L"'");
			args = &KArgsUsb();
			// Was considering using start-usb.script to make this option more user-friendly, but it doesn't integrate quite well enough yet
			//_LIT(KArgsUsb, "start-usb --verbose '" EXPAND(LCONS, FSHELL_LAUNCHER_SUPPORT_USB) L"'");
			//needsUnderlying = EFalse; // Indicating we're not doing a direct --console in args and thus don't need to use --underlying-console
#endif
			break;
		case EConnectionTcp:
#ifdef FSHELL_LAUNCHER_SUPPORT_TCP
			_LIT(KArgsTcp, "--console vt100tcpcons --console-title port=8080");
			args = &KArgsTcp();
#endif
			break;
		case EConnectionBluetooth:
#ifdef FSHELL_LAUNCHER_SUPPORT_BT
			_LIT(KArgsBluetooth, "--console vt100btcons");
			args = &KArgsBluetooth();
#endif
			break;
		case EConnectionSerial:
#ifdef FSHELL_LAUNCHER_SUPPORT_SERIAL
			_LIT(KArgsSerial, "--console vt100cons --console-title '" EXPAND(LCONS, FSHELL_LAUNCHER_SUPPORT_SERIAL) L"'");
			args = &KArgsSerial();
#endif
			break;
		}
	_LIT(KUnderlyingArgsFormat, "--underlying-console guicons:servername=%S %S");
	_LIT(KDirectArgsFormat, "--console guicons --console-title servername=%S %S");
	TBuf<256> argsBuf;
	if (needsUnderlying)
		{
		argsBuf.Format(KUnderlyingArgsFormat, &KServerName, args);
		}
	else
		{
		argsBuf.Format(KDirectArgsFormat, &KServerName, args);
		}
	DEBUG_PRINT(_L("CConsoleWrapper::ConnectL args \"%S\""), &argsBuf);
	TRAPD(err, iProcess.Create(KProcess, argsBuf));
	if (KErrNone == err)
		{
		DEBUG_PRINTF("CConsoleWrapper::ConnectL 0x%08x resuming", this);
		iConnectionType = aConnectionType;
		iOpen = ETrue;
		iProcess.Resume();
		}
	else
		{
		DEBUG_PRINTF("CConsoleWrapper::ConnectL 0x%08x err %d", this, err);
		TBuf<32> errorMsg;
		errorMsg.Format(KErrorFormat, err);
		// TODO: display errorMsg somewhere
		}
	}

void CConsoleWrapper::CloseEndpoint()
	{
	if (iOpen)
		{
		DEBUG_PRINTF("CConsoleWrapper::CloseEndpoint 0x%08x", this);
		iProcessMonitor->Logoff();
		// Note that neither child process (outer or inner) will be closed, since
		// they have both at this point been resumed.
		RProcess currentProcess;
		if (currentProcess.HasCapability(ECapabilityPowerMgmt))
			{
			DEBUG_PRINTF("CConsoleWrapper::CloseEndpoint 0x%08x killing child process", this);
			iProcess.Kill(0);
			}
		else
			{
			DEBUG_PRINTF("CConsoleWrapper::CloseEndpoint 0x%08x don't have PowerMgmt, cannot kill child process", this);
			}
		iProcess.Close();
		iOpen = EFalse;
		}
	}

void CConsoleWrapper::ChildProcessEnded(TInt aReturnCode)
	{
	DEBUG_PRINTF("CConsoleWrapper::ChildProcessEnded 0x%08x returnCode %d", this, aReturnCode);
	// TODO: print this to iConsole?
	}

