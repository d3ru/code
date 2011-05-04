// fshell_launcherappui.cpp
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
#include <eikbtgpc.h>
#include <eikmenup.h>

const TInt KLskPosition = 0;
const TTimeIntervalMicroSeconds32 KIdleTimeOut(1*1000*1000);

static const TInt KConsoleBufferSize = 1024;
_LIT(KConsoleFont, "");

void CFShellLauncherAppUi::ConstructL()
	{
	BaseConstructL();

	// Calculate the size of the application window
	TRect rect = ClientRect();

	// Create the application view
	iAppView = CFShellLauncherAppView::NewL(*this, rect);
	AddToStackL(iAppView);

	// Enable iIdleTimer to make the app return to foreground after a set delay
	iIdleTimer = CPeriodic::NewL(CActive::EPriorityStandard);

	Cba()->SetCommandL(KLskPosition, EAknSoftkeyOptions, _L("Options"));

	iConsoleServer = StartGuiConsServerL(KServerName, *this);

	iAppView->ActivateL();
	iAppView->DrawNow();
	}

CFShellLauncherAppUi::~CFShellLauncherAppUi()
	{
	delete iConsoleServer;
	if(iAppView)
		{
		RemoveFromStack(iAppView);
		delete iAppView;
		}
	delete iIdleTimer;
	}

void CFShellLauncherAppUi::SetConsolePendingConnection(CConsoleWrapper* aConsole)
	{
	DEBUG_PRINTF("CFShellLauncherAppUi::SetConsolePendingConnection 0x%08x", aConsole);
	if (aConsole)
		{
		ASSERT(!iConsolePendingConnection);
		}
	iConsolePendingConnection = aConsole;
	}

void CFShellLauncherAppUi::HandleForegroundEventL(TBool /*aForeground*/)
	{
	// Don't understand what this was aiming to achieve... -Tomsci
	/*
	if(!aForeground && iIdleTimer)
		{
		if(!iIdleTimer->IsActive())
			{
			iIdleTimer->Start(KIdleTimeOut, 0, TCallBack(BackgroundCallBack, this) );
			}
		}
	*/
	}

TInt CFShellLauncherAppUi::BackgroundCallBack(TAny* aSelf)
	{
	CFShellLauncherAppUi* self = static_cast<CFShellLauncherAppUi*>(aSelf);
	self->BringToForeground();
	return KErrNone;
	}

void CFShellLauncherAppUi::BringToForeground()
	{
	iIdleTimer->Cancel();
	const TInt err = iCoeEnv->WsSession().SetWindowGroupOrdinalPosition(iCoeEnv->RootWin().Identifier(), 0);
	}

void CFShellLauncherAppUi::DynInitMenuPaneL(
    TInt /*aResourceId*/, CEikMenuPane* aMenuPane)
    {
    TInt position = 0;
    if(!aMenuPane->MenuItemExists(EAknCmdExit, position))
        {
        CEikMenuPaneItem::SData item;
        item.iText.Copy(_L("Exit"));
        item.iCommandId = EAknCmdExit;
        item.iFlags = 0;
        item.iCascadeId = 0;
        item.iExtraText = KNullDesC;
        aMenuPane->AddMenuItemL(item);
        }
    }

void CFShellLauncherAppUi::HandleCommandL(TInt aCommand)
	{
	switch (aCommand)
		{
		case EAknSoftkeyBack:
		case EEikCmdExit:
			Exit();
			break;
		}
	}

void CFShellLauncherAppUi::HandleNewConsoleL(CConsoleControl* aConsole)
	{
	DEBUG_PRINTF("CFShellLauncherAppUi::HandleNewConsoleL 0x%08x pending 0x%08x", aConsole, iConsolePendingConnection);
	if (iConsolePendingConnection)
		{
		iConsolePendingConnection->HandleNewConsoleL(aConsole);
		iConsolePendingConnection = NULL;
		}
	}

void CFShellLauncherAppUi::HandleConsoleClosed(CConsoleControl* aConsole)
	{
	DEBUG_PRINTF("CFShellLauncherAppUi::HandleConsoleClosed 0x%08x", aConsole);
	iAppView->HandleConsoleClosed(aConsole);
	}

TInt CFShellLauncherAppUi::GetConsoleBufferSize()
	{
	DEBUG_PRINTF("CFShellLauncherAppUi::GetConsoleBufferSize");
	return KConsoleBufferSize;
	}

const TDesC& CFShellLauncherAppUi::GetConsoleFont()
	{
	DEBUG_PRINTF("CFShellLauncherAppUi::GetConsoleFont");
	return KConsoleFont;
	}

void CFShellLauncherAppUi::HandleStatusPaneSizeChange()
	{
	iAppView->SetRect(ClientRect());
	}
