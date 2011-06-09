// tmapfile.cpp
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

#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#ifdef FSHELL_USE_QT_CMAPFILE
#include <fshell/iocli_qt.h>
#endif
#include <fshell/bsym.h>

using namespace IoUtils;
using namespace LtkUtils;

class CCmdTmapFile : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdTmapFile();
private:
	CCmdTmapFile();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual const TDesC& Description() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	TFileName2 iFileName;
	TUint iOffset;

	TBuf<256> iResult;
	CMapFile* iMapFile;
	};

#ifdef FSHELL_USE_QT_CMAPFILE
QT_EXE_BOILER_PLATE(CCmdTmapFile)
#else
EXE_BOILER_PLATE(CCmdTmapFile)
#endif

CCommandBase* CCmdTmapFile::NewLC()
	{
	CCmdTmapFile* self = new(ELeave) CCmdTmapFile();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdTmapFile::~CCmdTmapFile()
	{
	delete iMapFile;
	}

CCmdTmapFile::CCmdTmapFile()
	{
	}

const TDesC& CCmdTmapFile::Name() const
	{
	_LIT(KName, "tmapfile");	
	return KName;
	}

const TDesC& CCmdTmapFile::Description() const
	{
	_LIT(KDescription, "Test for CMapFile class");
	return KDescription;
	}

void CCmdTmapFile::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendFileNameL(iFileName, _L("filename"), _L("map file to open"));
	aArguments.AppendUintL(iOffset, _L("offset"), _L("Offset in codesegment"));
	}

void CCmdTmapFile::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	//TODO: aOptions.AppendBoolL(iBoolOption, 'o' _L("long-option-name"), _L("description goes here"));
	}

void CCmdTmapFile::DoRunL()
	{
	// TODO: Add implementation.

	TRAPL(iMapFile = CMapFile::NewL(FsL(), iFileName), _L("Couldn't create CMapFile"));

	iMapFile->Lookup(iOffset, iResult);
	Printf(_L("CMapFile::Lookup(%d) returned: %S\r\n"), iOffset, &iResult);
	}
