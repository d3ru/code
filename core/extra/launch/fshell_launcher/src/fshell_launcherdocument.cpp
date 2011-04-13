// fshell_launcherdocument.cpp
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

#include "fshell_launcherdocument.h"
#include "fshell_launcherappui.h"

CFShellLauncherDocument::CFShellLauncherDocument(CEikApplication& aApp)
	: CAknDocument(aApp)
	{

	}

CEikAppUi* CFShellLauncherDocument::CreateAppUiL()
	{
	return new(ELeave) CFShellLauncherAppUi;
	}

