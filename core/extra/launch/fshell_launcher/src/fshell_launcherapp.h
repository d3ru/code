// fshell_launcherapp.h
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

#ifndef FSHELL_LAUNCHERAPP_H
#define FSHELL_LAUNCHERAPP_H

#include <aknapp.h>

class CFShellLauncherApplication
:	public CAknApplication
	{
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
	};

#endif // FSHELL_LAUNCHERAPP_H

