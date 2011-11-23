// LoggingAllocatorDummyLocale.cpp
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

#include <kernel/localise.h>
#include <fshell/loggingallocator.h>
#include <e32debug.h>

// Logging allocator 'proper' functions follow

TInt InstallLoggingAllocator(TUint aFlags)
	{
	//RDebug::Printf("+InstallLoggingAllocator");
	TInt ret = RLoggingAllocator::Install_WeakLink(aFlags);
	//RDebug::Printf("-InstallLoggingAllocator");
	return ret;
	}

//

EXPORT_C TLanguage Locl::Language()
	{
	return ELangTest;
	}

EXPORT_C void Locl::LocaleData(SLocaleData* /*aLocale*/)
	{
	}

typedef TInt (*TFunctionsThatDontHateMe)(TUint);
const TFunctionsThatDontHateMe KFns [] =
	{
	InstallLoggingAllocator,
	NULL // Acts as the zero termination
	};

EXPORT_C const TText * Locl::CurrencySymbol()
	{
	// Mangle mangle
	return (const TText *)&KFns[0];
	}

EXPORT_C const TText* Locl::ShortDateFormatSpec()
	{
	return _S("");
	}

EXPORT_C const TText* Locl::LongDateFormatSpec()
	{
	return _S("");
	}

EXPORT_C const TText* Locl::TimeFormatSpec()
	{
	return _S("");
	}

EXPORT_C const TFatUtilityFunctions* Locl::FatUtilityFunctions()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::DateSuffixTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::DayTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::DayAbbTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::MonthTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::MonthAbbTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::AmPmTable()
	{
	return NULL;
	}

EXPORT_C const TText * const * Locl::MsgTable()
	{
	return NULL;
	}

EXPORT_C const LCharSet* Locl::CharSet()
	{
	return NULL;
	}

EXPORT_C const TUint8 * Locl::TypeTable()
	{
	return NULL;
	}


EXPORT_C const TText * Locl::UpperTable()
	{
	return NULL;
	}

EXPORT_C const TText * Locl::LowerTable()
	{
	return NULL;
	}

EXPORT_C const TText * Locl::FoldTable()
	{
	return NULL;
	}

EXPORT_C const TText * Locl::CollTable()
	{
	return NULL;
	}

EXPORT_C TBool Locl::UniCode()
	{
	return ETrue;
	}
