// tloggingallocator.cpp
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
#include <fshell/heaputils.h>
#include <fshell/ltkutils.h>
#include <fshell/loggingallocator.h>

using namespace IoUtils;
using LtkUtils::RAllocatorHelper;

class CCmdTLoggingAllocator : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdTLoggingAllocator();
private:
	CCmdTLoggingAllocator();
	void LeakL();
	void Delete1(CBase* aObj);
	void Delete2(CBase* aObj);

private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual const TDesC& Description() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	TBool iTestLeak;
	TBool iTestDoubleDelete;
	TBool iTestSpike;
	};

EXE_BOILER_PLATE(CCmdTLoggingAllocator)

CCommandBase* CCmdTLoggingAllocator::NewLC()
	{
	CCmdTLoggingAllocator* self = new(ELeave) CCmdTLoggingAllocator();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdTLoggingAllocator::~CCmdTLoggingAllocator()
	{
	}

CCmdTLoggingAllocator::CCmdTLoggingAllocator()
	: CCommandBase(EManualComplete)
	{
	}

const TDesC& CCmdTLoggingAllocator::Name() const
	{
	_LIT(KName, "tloggingallocator");	
	return KName;
	}

const TDesC& CCmdTLoggingAllocator::Description() const
	{
	_LIT(KDescription, "Test code for RLoggingAllocator");
	return KDescription;
	}

void CCmdTLoggingAllocator::ArgumentsL(RCommandArgumentList& /*aArguments*/)
	{
	}

void CCmdTLoggingAllocator::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iTestLeak, 'l', _L("leak"), _L("Leak an allocation"));
	aOptions.AppendBoolL(iTestDoubleDelete, 'd', _L("double"), _L("Delete an allocation twice (will probably cause crash"));
	aOptions.AppendBoolL(iTestSpike, 's', _L("spike"), _L("Loop allocs and frees testing and waiting for heaptrace to install logging allocator"));
	}

void CCmdTLoggingAllocator::DoRunL()
	{
	_LIT(KInfo, "Allocator ptr = 0x%08x\r\nPid = %Ld\r\n");
	Printf(KInfo, &User::Allocator(), (TUint64)RProcess().Id());

	if (iTestSpike)
		{
		for (;;)
			{
			User::After(5000000);
			TAny* ptr = User::Alloc(10);
			User::Free(ptr);
			Printf(KInfo, &User::Allocator(), (TUint64)RProcess().Id());
			}
		}

	LeaveIfErr(RLoggingAllocator::Install_WeakLink(), _L("Couldn't install RLoggingAllocator"));
	
	if (iTestLeak) LeakL();
	
	if (iTestDoubleDelete)
		{
		CIdle* obj = CIdle::NewL(0);
		Delete1(obj);
		Delete2(obj);
		}
	}

// Separate fns so it's easier to check the right stuff is coming out in the stack trace
void CCmdTLoggingAllocator::LeakL()
	{
	User::AllocL(1000);
	}

void CCmdTLoggingAllocator::Delete1(CBase* aObj)
	{
	delete aObj;
	}

void CCmdTLoggingAllocator::Delete2(CBase* aObj)
	{
	delete aObj;
	}
