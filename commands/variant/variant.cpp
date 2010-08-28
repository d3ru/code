// variant.cpp
// 
// Copyright (c) 2008 - 2010 Accenture. All rights reserved.
// This component and the accompanying materials are made available
// under the terms of the "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
// 
// Initial Contributors:
// Accenture - Initial contribution
//

#include <hal.h>
#include "variant.h"
#include <fshell/descriptorutils.h>

//
// CCmdVariant
//
CCommandBase* CCmdVariant::NewLC()
	{
	CCmdVariant* self = new (ELeave) CCmdVariant();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdVariant::~CCmdVariant()
	{
	iVariant.ResetAndDestroy();
	}

CCmdVariant::CCmdVariant() : 
CCommandBase(CCommandBase::EManualComplete)
	{
	}

const TDesC& CCmdVariant::Name() const
	{
	_LIT(KName, "variant");	
	return KName;
	}

//
// CCmdVariant::DoRunL
// looks for a match between the variant uids specified via the command line and the
// actual variant uid of the device
//

struct TVariant
	{
	TInt iUid;
	LtkUtils::SLitC iName;
	};

// Only shipping products or established reference hardware platforms in here please.
// Unannounced protos should have their platform build its own variant.exe to identify their hardware,
// alternatively people have to rely on doing a "variant --uid xyz" whichever is preferred by the platform
const TVariant KMachineIdVariants[] = {
	{ 0x102734E3, DESC("h4") },
	{ 0x10286564, DESC("h6") },
	{ 0x102864F7, DESC("naviengine") },
	{ 0x20002D82, DESC("n96") },
	{ 0x20002496, DESC("e90") },
	{ 0x20002D7E, DESC("6120") },
	{ 0x20002D83, DESC("n81") },
	{ 0x2000DA56, DESC("5800") },
	{ 0x2001F0A1, DESC("satio") }, // I assume this is what it shipped with...
	{ 0x20029a73, DESC("n8") }, // Likewise...
	{ 0x102734e3, DESC("qemu") },
	};
const TInt KMachineIdVariantCount = sizeof(KMachineIdVariants) / sizeof(TVariant);

void CCmdVariant::DoRunL()
	{
	if (iList)
		{
		Printf(_L("Supported variants: "));
		for (TInt i = 0; i < KMachineIdVariantCount; i++)
			{
			Printf(_L("%S, "), &KMachineIdVariants[i].iName);
			}
		// Finally add the ones which don't appear in KMachineIdVariants
		Write(_L("wins, target\r\n"));
		Complete(KErrNone);
		return;
		}

	if (iMachineId.Count() == 0 && iVariant.Count() == 0)
		{
		LeaveIfErr(KErrArgument, _L("You must specify at least one <variantname> argument or --uid option"));
		}

	TInt localMachineUid = GetMachineUidL();
	TBool match = EFalse;
	if (iMachineId.Count())
		{
		for (TInt i = 0; i < iMachineId.Count(); i++)
			{
			if (iMachineId[i] == (TUint)localMachineUid)
				{
				match = ETrue;
				break;
				}
			}
		}

	if (iVariant.Count())
		{
		for (TInt i = 0; match == EFalse && i < iVariant.Count(); i++)
			{
			for (TInt j = 0; match == EFalse && j < KMachineIdVariantCount; j++)
				{
				if (iVariant[i]->CompareF(KMachineIdVariants[j].iName) == 0 && localMachineUid == KMachineIdVariants[j].iUid)
					{
					match = ETrue;
					}
				}
#ifdef __WINS__
			if (iVariant[i]->CompareF(_L("wins")) == 0) match = ETrue;
#else
			if (iVariant[i]->CompareF(_L("target")) == 0) match = ETrue;
#endif
			}
		}

	if (iVerbose)
		{
		if (match)
			{
			Printf(_L("Specified variant supported\r\n"));
			}
		else
			{
			PrintError(KErrNotSupported, _L("Specified variant not supported"));
			}
		}

	SetErrorReported(ETrue); // Don't show the error
	Complete(match ? KErrNone : KErrNotSupported);
	}

void CCmdVariant::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendStringL(iVariant, _L("variantname"));
	}

void CCmdVariant::OptionsL(RCommandOptionList& aOptions)
	{
	_LIT(KCmdOptUid, "uid");
	aOptions.AppendUintL(iMachineId, KCmdOptUid);
	_LIT(KOptVerbose, "verbose");
	aOptions.AppendBoolL(iVerbose, KOptVerbose);
	_LIT(KOptList, "list");
	aOptions.AppendBoolL(iList, KOptList);
	}

//
// CCmdVariant::GetMachineUidL
// retrieve the variant's machine uid
//
TInt CCmdVariant::GetMachineUidL()
	{
	TInt value = KErrNotSupported;
	User::LeaveIfError(HAL::Get(HALData::EMachineUid, value));
	return value;
	}
	
EXE_BOILER_PLATE(CCmdVariant)
