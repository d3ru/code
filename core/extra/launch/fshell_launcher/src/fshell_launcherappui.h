// fshell_launcherappui.h
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

#ifndef FSHELL_LAUNCHERAPPUI_H
#define FSHELL_LAUNCHERAPPUI_H

#include <aknappui.h>
#include <fshell_launcher.rsg>
#include <fshell/guicons.h>

class CFShellLauncherAppView;
class CConsoleWrapper;

class CFShellLauncherAppUi
:	public CAknAppUi
,	public MConsoleUi
	{
public:
	void ConstructL();
	~CFShellLauncherAppUi();
	void SetConsolePendingConnection(CConsoleWrapper* aConsole);

private:
	// CEikAppUi
	void DynInitMenuPaneL(TInt aResourceId, CEikMenuPane* aMenuPane);
	void HandleCommandL(TInt aCommand);
	void HandleForegroundEventL(TBool aForeground);
	static TInt BackgroundCallBack(TAny* aSelf);
	void BringToForeground();

private:
	// MConsoleUi
	void HandleNewConsoleL(CConsoleControl* aConsole);
	void HandleConsoleClosed(CConsoleControl* aConsole);
	TInt GetConsoleBufferSize();
	const TDesC& GetConsoleFont();

private:
	CFShellLauncherAppView* iAppView;
	CPeriodic* iIdleTimer;
	CServer2* iConsoleServer;
	CConsoleWrapper* iConsolePendingConnection;
	};

#endif // FSHELL_LAUNCHERAPPUI_H

