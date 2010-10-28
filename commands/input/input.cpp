// input.cpp
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

#include <e32keys.h>
#include <fshell/ioutils.h>
#include <fshell/common.mmh>
#include <fshell/ltkutils.h>

using namespace IoUtils;


class TKeyMapping
	{
public:
	TUint16 iConsoleKey;
	TUint16 iScanCode;
	TUint16 iModifiers;
#ifdef __WINS__
	const TText* iLabel;
#else
	const wchar_t* iLabel;
#endif
	};

const TKeyMapping KKeyMappings[] = 
	{
		{ '0', '0',	0, L"Zero" },
		{ '1', '1',	0, L"One" },
		{ '2', '2',	0, L"Two" },
		{ '3', '3',	0, L"Three" },
		{ '4', '4',	0, L"Four" },
		{ '5', '5',	0, L"Five" },
		{ '6', '6',	0, L"Six" },
		{ '7', '7',	0, L"Seven" },
		{ '8', '8',	0, L"Eight" },
		{ '9', '9',	0, L"Nine" },
		{ '*', '*',	0, L"Star" },
		{ '#', '#',	0, L"Hash" },
		{ EKeyUpArrow, EStdKeyUpArrow,	0, L"Up" },
		{ EKeyDownArrow, EStdKeyDownArrow,	0, L"Down" },
		{ EKeyLeftArrow, EStdKeyLeftArrow,	0, L"Left" },
		{ EKeyRightArrow, EStdKeyRightArrow,	0, L"Right" },
#ifdef LTK_PLATFORM_DCM
		{ EKeyEnter, 167,	0, L"Enter" },
		{ 'o', 164,	0, L"Menu" },
		{ 'p', 165,	0, L"Camera" },
		{ 'l', 228,	0, L"Mail" },
		{ ';', 229,	0, L"IMode" },
		{ ',', 196,	0, L"Call" },
		{ '.', EStdKeyBackspace, 0, L"Clear" },
		{ '/', 197,	0, L"End" },
		{ 'm', 230,	0, L"Multi" },
#endif
	};
const TInt KNumKeyMappings = sizeof(KKeyMappings) / sizeof(TKeyMapping);


class CCmdInput : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdInput();
private:
	CCmdInput();
	void ShowKeyMappings();
	void SimulateKeyL(TInt aConsoleKey);
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	TUint iConsoleKey;
	TBool iShow;
	TUint iScanCode;
	TUint iModifiers;
	};

EXE_BOILER_PLATE(CCmdInput)

CCommandBase* CCmdInput::NewLC()
	{
	CCmdInput* self = new(ELeave) CCmdInput();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdInput::~CCmdInput()
	{
	}

CCmdInput::CCmdInput()
	{
	}

void CCmdInput::ShowKeyMappings()
	{
	IoUtils::CTextBuffer* buf = IoUtils::CTextBuffer::NewLC(0x100);

	buf->AppendL(_L("Input key\tScan Code\tModifiers\tLabel\r\n"));
	for (TInt i = 0; i < KNumKeyMappings; ++i)
		{
		const TKeyMapping& mapping = KKeyMappings[i];
		if (mapping.iConsoleKey == EKeyUpArrow)
			{
			buf->AppendFormatL(_L("Up (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		else if (mapping.iConsoleKey == EKeyDownArrow)
			{
			buf->AppendFormatL(_L("Down (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		else if (mapping.iConsoleKey == EKeyLeftArrow)
			{
			buf->AppendFormatL(_L("Left (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		else if (mapping.iConsoleKey == EKeyRightArrow)
			{
			buf->AppendFormatL(_L("Right (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		else if (mapping.iConsoleKey == EKeyEnter)
			{
			buf->AppendFormatL(_L("Enter (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		else
			{
			buf->AppendFormatL(_L("%c (0x%x)\t0x%x\t0x%x\t%s\r\n"), mapping.iConsoleKey, mapping.iConsoleKey, mapping.iScanCode, mapping.iModifiers, mapping.iLabel);
			}
		}

	CTextFormatter* formatter = CTextFormatter::NewLC(Stdout());
	formatter->TabulateL(0, 2, buf->Descriptor());
	Write(formatter->Descriptor());

	CleanupStack::PopAndDestroy(2, buf);
	}

void CCmdInput::SimulateKeyL(TInt aConsoleKey)
	{
	for (TInt i = 0; i < KNumKeyMappings; ++i)
		{
		const TKeyMapping& mapping = KKeyMappings[i];
		if (aConsoleKey == mapping.iConsoleKey)
			{
			LtkUtils::InjectRawKeyEvent(mapping.iScanCode, mapping.iModifiers, 0);
			break;
			}
		}
	}

const TDesC& CCmdInput::Name() const
	{
	_LIT(KName, "input");	
	return KName;
	}

void CCmdInput::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendUintL(iConsoleKey, _L("key"));
	}

void CCmdInput::OptionsL(RCommandOptionList& aOptions)
	{
	aOptions.AppendBoolL(iShow, _L("show"));
	aOptions.AppendUintL(iScanCode, _L("scan-code"));
	aOptions.AppendUintL(iModifiers, _L("modifiers"));
	}

void CCmdInput::DoRunL()
	{
	if (iShow)
		{
		ShowKeyMappings();
		}
	else if (iOptions.IsPresent(&iScanCode))
		{
		LtkUtils::InjectRawKeyEvent(iScanCode, iModifiers, 0);
		}
	else
		{
		if (iArguments.IsPresent(&iConsoleKey))
			{
			SimulateKeyL(iConsoleKey);
			}
		else
			{
			RIoConsoleReadHandle& stdin = Stdin();
			stdin.SetReadModeL(RIoReadHandle::EFull);
			TBuf<1> buf;
			TInt err = KErrNone;
			while (err == KErrNone)
				{
				err = stdin.Read(buf);
				if (err == KErrNone)
					{
					if (buf[0] == 'q')
						{
						break;
						}
					else
						{
						SimulateKeyL(buf[0]);
						}
					}
				}
			}
		}
	}
