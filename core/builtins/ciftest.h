// ciftest.h
//
// Copyright (c) 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#ifndef CIFTEST_H
#define CIFTEST_H

#include <fshell/ioutils.h>
#include <fshell/stringhash.h>
#include <fshell/descriptorutils.h>
#include "parser.h"

using namespace IoUtils;
using LtkUtils::RStringHash;
using LtkUtils::RLtkBuf;

class CCmdCifTest : public CCommandBase, public MParserObserver
	{
public:
	static CCommandBase* NewLC();
	~CCmdCifTest();
private:
	CCmdCifTest();
	void TestCifL(CCommandInfoFile* aCif);
	void NextCif();
	void TestCompleted(TInt aError);
private: // From CCommandBase.
	void RunL();
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private: // From MParserObserver.
	virtual void HandleParserComplete(CParser& aParser, const TError& aError);

private:
	HBufC* iCmd;
	TBool iVerbose;
	TBool iKeepGoing;
	TBool iAll;

	TFileName iFileName;
	CCommandInfoFile* iCurrentCif;
	CParser* iParser;
	CEnvironment* iEnvForScript;
	RPointerArray<HBufC> iCifFiles;
	RStringHash<HBufC*> iCifNameHash; // Maps cif name to the full path HBufC* in iCifFiles
	RLtkBuf iDummyScriptBuf;

	TInt iPasses;
	TInt iFailures;
	TInt iNextCif;
	TInt iFirstError;
	};

#endif
