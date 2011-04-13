// fshell_launcherdocument.h
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

#ifndef FSHELL_LAUNCHERDOCUMENT_H
#define FSHELL_LAUNCHERDOCUMENT_H

#include <akndoc.h>

class CFShellLauncherDocument
:	public CAknDocument
	{
public:
	CFShellLauncherDocument(CEikApplication& aApp);

private:
	CEikAppUi* CreateAppUiL();
	};

#endif //FSHELL_LAUNCHERDOCUMENT_H

