// tokens.cpp
// 
// Copyright (c) 2010 - 2011 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

// The simpler tokens are given at the start. An implementation that shortens a string using tokens should search for matches from the bottom of this list up, to be certain of replacing the longest possible string

// This list may be modified without needing to bump the bsym major version number, because the table is copied into the bsym file itself.

char const * const KTokens [] = {
	"void",
	"char",
	"unsigned",
	"long",
	"int",
	"const",
	" const",
	" stub",
	"TDesC8",
	"TDesC16",
	"TRequestStatus",
	"Cancel",
	"unsigned char",
	"unsigned int",
	"unsigned long",
	"_E32Dll",
	"_E32Dll_Body",
	"_E32Startup",
	"void *",
	"void*",
	"void const *",
	"const void*",
	"const TDesC16&",
	"TDesC16 const &",
	"const TDesC8&",
	"TDesC8 const &",
	"ConstructL",
	"thunk{",
	"thunk{-",
	"} to ",
	"operator [](",
	"operator new(",
	"operator ==(",
	"TAlignedBuf8(",
	"ResetAndDestroy()",
	"CleanupClosePushL<",
	"Image$$ER_RO$$Base",
	"__cpp_initialize__aeabi_",
	"__DLL_Export_Table__",
	"TRefByValue<const TDesC8>)",
	"TRefByValue<const TDesC16>)",
	"TRefByValue<TDesC8 const>)",
	"TRefByValue<TDesC16 const>)",
	"TRefByValue<const TDesC8>, ...)",
	"TRefByValue<const TDesC16>, ...)",
	"TRefByValue<TDesC8 const>,...)",
	"TRefByValue<TDesC16 const>,...)",
	"__deallocating()",
	"__sub_object(",
	"typeinfo for ",
	"typeinfo name for ",
	"vtable for ",
	" virtual table",
	"\\epoc32\\release\\",
	"\\epoc32\\release\\gcce\\urel\\",
	"\\epoc32\\release\\armv5\\urel\\",
	"\\epoc32\\release\\armv5.",
	"\\epoc32\\release\\armv5smp\\urel\\",
	"\\sys\\bin\\",
	"z:\\sys\\bin\\",
    " (EXPORTED)",
	};

char const*const*const Tokens()
	{
	return &KTokens[0];
	}
	
extern const int KTokenCount = sizeof(KTokens)/sizeof(char*);
