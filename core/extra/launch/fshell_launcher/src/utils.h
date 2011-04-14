// utils.h
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

#ifndef UTILS_H
#define UTILS_H

#include <e32std.h>
#include <fshell/descriptorutils.h>

enum TConnectionType
	{
	EConnectionUsb,
	EConnectionTcp,
	EConnectionBluetooth,
	EConnectionSerial
	};

const TInt KNumConnectionTypes = 4;

extern const LtkUtils::SLitC KTextConnectionType[KNumConnectionTypes];

_LIT(KServerName, "guicons-fshell-launcher");

#ifdef _DEBUG
#	define DEBUG_PRINT(ARGS...) RDebug::Print(ARGS);
#	define DEBUG_PRINTF(ARGS...) RDebug::Printf(ARGS);
#else
#	define DEBUG_PRINT(ARGS...)
#	define DEBUG_PRINTF(ARGS...)
#endif

#undef ASSERT
#define ASSERT(x) if (!(x)) { DEBUG_PRINTF("Assertion '%s' failed at line %d", #x, __LINE__); User::Invariant(); }

#endif // UTILS_H

