// consolewrapper.h
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

#ifndef CONSOLEWRAPPER_H
#define CONSOLEWRAPPER_H

#include <coecntrl.h>
#include <coecobs.h>
#include "utils.h"

class CCloseButton;
class CConsoleControl;
class CDelayedDestroy;
class CProcessMonitor;
class CFShellLauncherAppView;

class CConsoleWrapper
:	public CCoeControl
,	public MCoeControlObserver
	{
	friend class CProcessMonitor;
public:
	static CConsoleWrapper* NewLC(CFShellLauncherAppView& aView);
	~CConsoleWrapper();
	TInt CountComponentControls() const;
	CCoeControl* ComponentControl(TInt aIndex) const;
	void OpenEndpointL(TConnectionType aConnectionType);
	CConsoleControl* Console() const;
	void HandleNewConsoleL(CConsoleControl* aConsole);
	void HandleConsoleClosed(CConsoleControl* aConsole);
	void DelayedDestroy();

private:
	// CCoeControl
	void SizeChanged();
	void Draw(const TRect& aRect) const;

private:
	// MCoeControlObserver
	void HandleControlEventL(CCoeControl *aControl, TCoeEvent aEventType);

private:
	CConsoleWrapper(CFShellLauncherAppView& aView);
	void ConstructL();
	void Layout();
	void CloseEndpoint();
	void ChildProcessEnded(TInt aReturnCode);

private:
	CFShellLauncherAppView& iView;
	CCloseButton* iCloseButton;
	CConsoleControl* iConsole;
	TConnectionType iConnectionType;
	CProcessMonitor* iProcessMonitor;
	RProcess iProcess;
	TBool iOpen;
	CDelayedDestroy* iDestroy;
	};

#endif // CONSOLEWRAPPER_H

