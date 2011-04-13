// fshell_launcherview.h
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

#ifndef FSHELL_LAUNCHERVIEW_H
#define FSHELL_LAUNCHERVIEW_H

#include <coecntrl.h>
#include <coecobs.h>
#include <eiksbfrm.h>

class CConnectButton;
class CConsoleControl;
class CConsoleWrapper;
class CFShellLauncherAppUi;

class CFShellLauncherAppView
:	public CCoeControl
,	public MCoeControlObserver
,	public MEikScrollBarObserver
	{
public:
	static CFShellLauncherAppView* NewL(CFShellLauncherAppUi& aUi, const TRect& aRect);
	~CFShellLauncherAppView();
	TInt CountComponentControls() const;
	CCoeControl* ComponentControl(TInt aIndex) const;
	void CloseConsoleL(CConsoleWrapper& aConsole);
	void HandleConsoleClosed(CConsoleControl* aConsole);

private:
	// CCoeControl
	void SizeChanged();
	void Draw(const TRect& aRect) const;

private:
	// MCoeControlObserver
	void HandleControlEventL(CCoeControl *aControl, TCoeEvent aEventType);

private:
	// MEikScrollBarObserver
	void HandleScrollEventL(CEikScrollBar* aScrollBar, TEikScrollEvent aEventType);

private:
	CFShellLauncherAppView(CFShellLauncherAppUi& aUi);
	void ConstructL(const TRect& aRect);
	void CreateButtonsL();
	void CreateScrollBarFrameL();
	void LayoutL();
	void UpdateScrollBarFrameL();
	void ScrollPositionChangedL(TInt aIndex);

private:
	CFShellLauncherAppUi& iUi;
	CEikScrollBarFrame* iScrollBarFrame;
	TAknDoubleSpanScrollBarModel iVScrollBarModel;
	TAknDoubleSpanScrollBarModel iHScrollBarModel;
	TInt iFirstVisibleControlIndex;
	RPointerArray<CCoeControl> iControls;
	RPointerArray<CConnectButton> iButtons;
	RPointerArray<CConsoleWrapper> iConsoles;
	};

#endif //FSHELL_LAUNCHERVIEW_H

