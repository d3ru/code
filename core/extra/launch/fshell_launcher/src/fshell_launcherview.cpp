// fshell_launcherview.cpp
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
#include "fshell_launcherappui.h"
#include "fshell_launcherview.h"
#include "utils.h"
#include <eikenv.h>
#include <aknbutton.h>
#include <fshell/common.mmh>
#include <fshell/guicons.h>

static const TInt KLayoutPadding = 10;
static const TSize KConnectButtonSize(150, 50);

class CConnectButton : public CAknButton
	{
public:
	static CConnectButton* NewLC(CCoeControl& aParent, TConnectionType aConnectionType, const TDesC& aText);
	TConnectionType ConnectionType() const { return iConnectionType; }
private:
	CConnectButton(TConnectionType aConnectionType);
	void ConstructL(CCoeControl& aParent, const TDesC& aText);
private:
	const TConnectionType iConnectionType;
	};

CConnectButton* CConnectButton::NewLC(CCoeControl& aParent, TConnectionType aConnectionType, const TDesC& aText)
	{
	CConnectButton* self = new (ELeave) CConnectButton(aConnectionType);
	CleanupStack::PushL(self);
	self->ConstructL(aParent, aText);
	return self;
	}

CConnectButton::CConnectButton(TConnectionType aConnectionType)
:	CAknButton(0)
,	iConnectionType(aConnectionType)
	{

	}

void CConnectButton::ConstructL(CCoeControl& aParent, const TDesC& aText)
	{
	CAknButton::ConstructL(0, 0, 0, 0, aText, aText, 0);
	SetContainerWindowL(aParent);
	SetSize(KConnectButtonSize);
	ActivateL();
	}

CFShellLauncherAppView* CFShellLauncherAppView::NewL(CFShellLauncherAppUi& aUi, const TRect& aRect)
	{
	CFShellLauncherAppView* self = new(ELeave) CFShellLauncherAppView(aUi);
	CleanupStack::PushL(self);
	self->ConstructL(aRect);
	CleanupStack::Pop();
	return self;
	}

CFShellLauncherAppView::CFShellLauncherAppView(CFShellLauncherAppUi& aUi)
:	iUi(aUi)
	{

	}

void CFShellLauncherAppView::ConstructL(const TRect& aRect)
	{
    CreateWindowL();
    CreateButtonsL();
    CreateScrollBarFrameL();
	SetRect(aRect);
	}

void CFShellLauncherAppView::CreateButtonsL()
	{
    for (TInt i=0; i<KNumConnectionTypes; ++i)
    	{
    	const TConnectionType type = static_cast<TConnectionType>(i);
    	const TDesC& text = KTextConnectionType[i];
    	const TDesC* textPtr = NULL;
    	switch (type)
    		{
    		case EConnectionUsb:
#ifdef FSHELL_LAUNCHER_SUPPORT_USB
    			textPtr = &text;
#endif
    			break;
			case EConnectionTcp:
#ifdef FSHELL_LAUNCHER_SUPPORT_TCP
				textPtr = &text;
#endif
				break;
			case EConnectionBluetooth:
#ifdef FSHELL_LAUNCHER_SUPPORT_BT
				textPtr = &text;
#endif
				break;
			case EConnectionSerial:
#ifdef FSHELL_LAUNCHER_SUPPORT_SERIAL
				textPtr = &text;
#endif
				break;
    		}
    	if (textPtr)
    		{
    		CConnectButton* button = CConnectButton::NewLC(*this, type, *textPtr);
    		button->SetObserver(this);
			iControls.AppendL(button);
			iButtons.AppendL(button);
			CleanupStack::Pop(button);
    		}
    	}
	}

void CFShellLauncherAppView::CreateScrollBarFrameL()
	{
    iScrollBarFrame = new (ELeave) CEikScrollBarFrame(this, this);
    iScrollBarFrame->CreateDoubleSpanScrollBarsL(ETrue,		// window-owning
    		                                     EFalse,	// non-remote
    		                                     ETrue,		// vertical
    		                                     EFalse);	// non-horizontal
    iScrollBarFrame->SetTypeOfVScrollBar(CEikScrollBarFrame::EDoubleSpan);
	}

CFShellLauncherAppView::~CFShellLauncherAppView()
	{
	iConsoles.Close();
	iButtons.Close();
	iControls.ResetAndDestroy();
	delete iScrollBarFrame;
	}

TInt CFShellLauncherAppView::CountComponentControls() const
	{
	return iControls.Count();
	}

CCoeControl* CFShellLauncherAppView::ComponentControl(TInt aIndex) const
	{
	return iControls[aIndex];
	}

void CFShellLauncherAppView::CloseConsoleL(CConsoleWrapper& aConsole)
	{
	CCoeControl* control = &aConsole;
	const TInt index = iControls.Find(control);
	iControls.Remove(index);
	iConsoles.Remove(iConsoles.Find(&aConsole));
	aConsole.DelayedDestroy();
	LayoutL();
	}

void CFShellLauncherAppView::HandleConsoleClosed(CConsoleControl* aConsole)
	{
	DEBUG_PRINTF("CFShellLauncherAppView::HandleConsoleClosed 0x%08x", aConsole);
	TBool found = EFalse;
	for (TInt i=0; !found && i<iConsoles.Count(); ++i)
		{
		CConsoleWrapper* wrapper = iConsoles[i];
		if (wrapper->Console() == aConsole)
			{
			wrapper->HandleConsoleClosed(aConsole);
			found = ETrue;
			}
		}
	if (!found)
		{
		DEBUG_PRINTF("CFShellLauncherAppView::HandleConsoleClosed 0x%08x not found", aConsole);
		}
	}

void CFShellLauncherAppView::SizeChanged()
	{
	TRAP_IGNORE(LayoutL());
	}

void CFShellLauncherAppView::Draw(const TRect& aRect) const
	{
	CWindowGc& gc = SystemGc();
	gc.SetBrushColor(TRgb(0, 0, 0));
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.Clear(aRect);
	}

void CFShellLauncherAppView::HandleControlEventL(CCoeControl* aControl, TCoeEvent aEventType)
	{
	for (TInt i=0; i<iButtons.Count(); ++i)
		{
		CConnectButton* button = static_cast<CConnectButton*>(iButtons[i]);
		if (aControl == button && EEventStateChanged == aEventType)
			{
			DEBUG_PRINTF("CFShellLauncherAppView::HandleControlEventL connection type %d", button->ConnectionType());
			CConsoleWrapper* console = CConsoleWrapper::NewLC(*this);
			iUi.SetConsolePendingConnection(console);
			console->OpenEndpointL(button->ConnectionType());
			if (i == iButtons.Count() - 1)
				{
				iControls.AppendL(console);
				}
			else
				{
				TInt nextButtonPos = iControls.Find(iButtons[i+1]);
				iControls.InsertL(console, nextButtonPos);
				}
			CleanupStack::Pop(console);
			iConsoles.AppendL(console);
			LayoutL();
			}
		}
	}

void CFShellLauncherAppView::HandleScrollEventL(CEikScrollBar* aScrollBar, TEikScrollEvent aEventType)
	{
	switch( aEventType )
		{
		case EEikScrollHome :
			ScrollPositionChangedL(0);
			break;
		case EEikScrollEnd :
			ScrollPositionChangedL(CountComponentControls() - 1);
			break;
		default:
			ScrollPositionChangedL(aScrollBar->ThumbPosition());
			break;
		}
	}

void CFShellLauncherAppView::LayoutL()
    {
	TInt y = KLayoutPadding;
	for (TInt i=0; i<iControls.Count(); ++i)
		{
		CCoeControl* control = iControls[i];
		if (KErrNotFound == iButtons.Find(static_cast<CConnectButton*>(control)))
			{
			control->SetSize(TSize(Size().iWidth - 2*KLayoutPadding, control->Size().iHeight));
			}
		const TBool visible = (i >= iFirstVisibleControlIndex);
		control->MakeVisible(visible);
		if (visible)
			{
			control->SetPosition(TPoint(KLayoutPadding, y));
			y += (control->Size().iHeight + KLayoutPadding);
			}
		}
	UpdateScrollBarFrameL();
    DrawDeferred();
    }

void CFShellLauncherAppView::UpdateScrollBarFrameL()
	{
	TInt contentHeight = KLayoutPadding;
	for (TInt i=0; i<CountComponentControls(); ++i)
		{
		contentHeight += KLayoutPadding;
		contentHeight += iControls[CountComponentControls() - 1]->Size().iHeight;
		}
	const TInt height = Rect().Height();
	if (contentHeight > height &&
		iScrollBarFrame->VScrollBarVisibility() == CEikScrollBarFrame::EOff)
		{
		iScrollBarFrame->SetScrollBarVisibilityL(CEikScrollBarFrame::EOff,
		                                         CEikScrollBarFrame::EOn);
		}
	else if (contentHeight < height &&
			 iScrollBarFrame->VScrollBarVisibility() == CEikScrollBarFrame::EOn)
		{
		iScrollBarFrame->SetScrollBarVisibilityL(CEikScrollBarFrame::EOff,
				                                 CEikScrollBarFrame::EOff);
		}
	iVScrollBarModel.SetScrollSpan(CountComponentControls());
	iVScrollBarModel.SetWindowSize(1);
	iVScrollBarModel.SetFocusPosition(iFirstVisibleControlIndex);
	TEikScrollBarFrameLayout layout;
	layout.iTilingMode = TEikScrollBarFrameLayout::EInclusiveRectConstant;
	TRect rect = Rect();
	iScrollBarFrame->TileL(&iHScrollBarModel, &iVScrollBarModel, rect, rect, layout);
	iScrollBarFrame->SetVFocusPosToThumbPos(iVScrollBarModel.FocusPosition());
	}

void CFShellLauncherAppView::ScrollPositionChangedL(TInt aIndex)
	{
	iFirstVisibleControlIndex = aIndex;
	LayoutL();
	}
