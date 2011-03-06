// iocli_qt.h
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

#include <QtCore/QtGlobal>

namespace IoUtils
	{

IMPORT_C QtMsgHandler GetIosrvDebugHandler();

	}


// Only difference to EXE_BOILER_PLATE in ioutils.h is that it doesn't do the heap checks, as that goes right out of the window when using Qt APIs
// At some point it might also set up a QCoreApplication instead of a CTrapCleanup+CActiveScheduler
#define QT_EXE_BOILER_PLATE(__CLASS_NAME) \
void MainL()\
	{\
	CActiveScheduler* scheduler = new(ELeave) CActiveScheduler;\
	CleanupStack::PushL(scheduler);\
	CActiveScheduler::Install(scheduler);\
	IoUtils::CCommandBase* command = __CLASS_NAME::NewLC();\
	command->RunCommandL();\
	CleanupStack::PopAndDestroy(2, scheduler);\
	}\
\
GLDEF_C TInt E32Main()\
	{\
	TInt err = KErrNoMemory;\
	CTrapCleanup* cleanup = CTrapCleanup::New();\
	if (cleanup)\
		{\
		TRAP(err, MainL());\
		delete cleanup;\
		}\
	return err;\
	}\

