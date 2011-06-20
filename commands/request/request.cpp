// request.cpp
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

using namespace IoUtils;

class CCmdRequest : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdRequest();
private:
	CCmdRequest();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
	void DoCancel();
	void RunL();
private:
	RIoConsole iConsole;
	HBufC* iFileName;
	TFileName2 iLocalName;
	};

EXE_BOILER_PLATE(CCmdRequest)

CCommandBase* CCmdRequest::NewLC()
	{
	CCmdRequest* self = new(ELeave) CCmdRequest();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdRequest::~CCmdRequest()
	{
	delete iFileName;
	iConsole.Close();
	}

CCmdRequest::CCmdRequest()
	: CCommandBase(EManualComplete)
	{
	}

const TDesC& CCmdRequest::Name() const
	{
	_LIT(KName, "request");	
	return KName;
	}

void CCmdRequest::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iFileName, _L("filename"));
	aArguments.AppendFileNameL(iLocalName, _L("local-path"));
	}

void CCmdRequest::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	}

void CCmdRequest::DoRunL()
	{
	if (iFileName->Locate('\\') != -1 || iFileName->Locate('/') != -1)
		{
		LeaveIfErr(KErrArgument, _L("filename cannot contain any path information"));
		}
	if (iLocalName.Length() == 0)
		{
		_LIT(KDll, ".dll");
		_LIT(KExe, ".exe");
		_LIT(KLdd, ".ldd");
		_LIT(KCif, ".cif");
		TPtrC suff = iFileName->Right(4);
		if (suff.CompareC(KDll) == 0 || suff.CompareC(KExe) == 0 || suff.CompareC(KLdd) == 0)
			{
			iLocalName = _L("c:\\sys\\bin\\");
			iLocalName.Append(*iFileName);
			}
		else if (suff.CompareC(KCif) == 0)
			{
			iLocalName = _L("c:\\resource\\cif\\fshell\\");
			iLocalName.Append(*iFileName);
			}
		else
			{
			iLocalName = *iFileName;
			iLocalName.MakeAbsoluteL(FsL());
			}
		}

	LeaveIfErr(iConsole.Open(IoSession(), Stdout()), _L("Couldn't open handle to console"));
	iConsole.RequestFile(*iFileName, iLocalName, iStatus);
	SetActive();
	}

void CCmdRequest::DoCancel()
	{
	iConsole.CancelRequestFile();
	}

void CCmdRequest::RunL()
	{
	if (iStatus == KErrExtensionNotSupported)
		{
		Complete(iStatus.Int(), _L("The current console does not support the DataRequester extension."));
		}
	else if (iStatus.Int())
		{
		Complete(iStatus.Int(), _L("Failed to get %S"), &iLocalName);
		}
	else
		{
		Complete(KErrNone);
		}
	}
