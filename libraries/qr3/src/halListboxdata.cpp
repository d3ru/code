// halListboxdata.cpp
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
#include "KernLbxModel.h"
#include "Utils.h"
#include <fshell/clogger.h>
#include <fshell/memoryaccess.h>
#include <fshell/ltkhal.h>

CHalListBoxData::CHalListBoxData(CKernListBoxModel* aModel)
	: CKernListBoxData(aModel)
	{
	}

void CHalListBoxData::DoFormatL(TObjectKernelInfo* aInfo, RBuf& name, RBuf& more, TInt& itemId)
	{
	LtkUtils::CHalAttribute& info = *reinterpret_cast<LtkUtils::CHalAttribute*>(aInfo);
	name.Copy(info.AttributeName());
	more.Copy(info.DescriptionL());
	itemId = info.Attribute();
	}

void CHalListBoxData::DumpToCloggerL(RClogger& clogger, TInt i, TInt /*count*/)
	{
	_LIT(KHalDesc,"HAL;Number;Name;Value;ValueHumanReadable");
	_LIT(KHalFmt,"HAL;%i;%S;%i;%S;%i");

	if (i == 0) clogger.Log(KHalDesc);
	LtkUtils::CHalAttribute& info = *reinterpret_cast<LtkUtils::CHalAttribute*>(iInfo);
	clogger.Log(KHalFmt, info.Attribute(), &info.AttributeName(), info.Value(), &info.DescriptionL());
	}

void CHalListBoxData::DoInfoForDialogL(RBuf& aTitle, RBuf& inf, TDes* /*aName*/)
	{
	LtkUtils::CHalAttribute& info = *reinterpret_cast<LtkUtils::CHalAttribute*>(iInfo);
	_LIT(KInfo, "HAL info");
	aTitle.Copy(KInfo);
	_LIT(KHalFmt, "%S\n%S\n\n(Id: %i Val: %i/0x%x)");
	inf.Format(KHalFmt, &info.AttributeName(), &info.DescriptionL(), info.Attribute(), info.Value(), info.Value());
	}

CHalListBoxData::~CHalListBoxData()
	{
	LtkUtils::CHalAttribute* info = reinterpret_cast<LtkUtils::CHalAttribute*>(iInfo);
	delete info;
	iInfo = NULL;
	}
