// hcr.cpp
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

#include <fshell/memoryaccesscmd.h>

using namespace IoUtils;

class CCmdHcr : public CMemoryAccessCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdHcr();
private:
	CCmdHcr();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void OptionsL(RCommandOptionList& aOptions);
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
private:
	TUint iCategory;
	TUint iKey;
	TInt iArrayIndex;
	RBuf8 iData;
	};

CCommandBase* CCmdHcr::NewLC()
	{
	CCmdHcr* self = new(ELeave) CCmdHcr();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdHcr::~CCmdHcr()
	{
	iData.Close();
	}

CCmdHcr::CCmdHcr()
	{
	}

const TDesC& CCmdHcr::Name() const
	{
	_LIT(KName, "hcr");
	return KName;
	}

void CCmdHcr::DoRunL()
	{
	LoadMemoryAccessL();
	TInt ret;
	TInt err = iMemAccess.GetHcrValue(iCategory, iKey, ret);
	if (err == KErrNone)
		{
		Printf(_L("%d\r\n"), ret);
		}
	else if (err == KErrArgument)
		{
		// Maybe it's big data
		iData.CreateL(4000);
		TInt size = 0;
		RMemoryAccess::THcrType type = RMemoryAccess::EHcrData;
		err = iMemAccess.GetHcrValue(iCategory, iKey, iData, type, size);
		if (!err)
			{
			switch (type)
				{
				case RMemoryAccess::EHcrArray:
					{
					TInt len = iData.Length() / 4;
					for (TInt i = 0; i < len; i++)
						{
						if (!iOptions.IsPresent(&iArrayIndex) || iArrayIndex == i)
							{
							Printf(_L("%d\r\n"), ((const TInt*)iData.Ptr())[i]);
							}
						}
					break;
					}
				case RMemoryAccess::EHcrString:
					Printf(_L8("%S\r\n"), &iData);
					break;
				case RMemoryAccess::EHcrData:
					LtkUtils::HexDumpToOutput(iData, Stdout());
					Write(_L("\r\n"));
					break;
				case RMemoryAccess::EHcrInt64:
					{
					TInt64 res;
					memcpy(&res, iData.Ptr(), 8);
					Printf(_L("%Ld\r\n"), res);
					break;
					}
				default:
					break;
				}
			}
		else if (err == KErrOverflow)
			{
			LeaveIfErr(err, _L("Overflow on key %d with type %d and size %d"), iKey, type, size);
			}
		}
	LeaveIfErr(err, _L("Couldn't read HCR"));
	}

void CCmdHcr::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendIntL(iArrayIndex, _L("index"));
	}

void CCmdHcr::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendUintL(iCategory, _L("category"));
	aArguments.AppendUintL(iKey, _L("key"));
	}

EXE_BOILER_PLATE(CCmdHcr)

