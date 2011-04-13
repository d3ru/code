// fshell_launcherapp.cpp
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

#include "fshell_launcherapp.h"
#include "fshell_launcherdocument.h"
#include <fshell/common.mmh>

const TUid KUidFShellLauncher = { FSHELL_UID_FSHELL_LAUNCHER };

TUid CFShellLauncherApplication::AppDllUid() const
	{
	return KUidFShellLauncher;
	}

CApaDocument* CFShellLauncherApplication::CreateDocumentL()
	{
	return new (ELeave) CFShellLauncherDocument(*this);
	}

