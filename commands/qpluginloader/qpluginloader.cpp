// qpluginloader.cpp
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
#include <fshell/iocli_qt.h>
#include <fshell/common.mmh>
#include <QObject>
#include <QPluginLoader>
#include <QDir>
#include <QCoreApplication>

using namespace IoUtils;

class CCmdQPluginLoader : public CCommandBase
	{
public:
	static CCommandBase* NewLC();
	~CCmdQPluginLoader();
private:
	CCmdQPluginLoader();
private: // From CCommandBase.
	virtual const TDesC& Name() const;
	virtual void DoRunL();
	virtual void ArgumentsL(RCommandArgumentList& aArguments);
	virtual void OptionsL(RCommandOptionList& aOptions);
private:
	QPluginLoader iLoader;
	TFileName2 iFileName;
	};

QT_EXE_BOILER_PLATE(CCmdQPluginLoader)

CCommandBase* CCmdQPluginLoader::NewLC()
	{
	CCmdQPluginLoader* self = new(ELeave) CCmdQPluginLoader();
	CleanupStack::PushL(self);
	self->BaseConstructL();
	return self;
	}

CCmdQPluginLoader::~CCmdQPluginLoader()
	{
	iLoader.unload();
	}

CCmdQPluginLoader::CCmdQPluginLoader()
	{
	}

const TDesC& CCmdQPluginLoader::Name() const
	{
	_LIT(KName, "qpluginloader");	
	return KName;
	}

void CCmdQPluginLoader::ArgumentsL(RCommandArgumentList& aArguments)
	{
	aArguments.AppendFileNameL(iFileName, _L("filename"));
	}

void CCmdQPluginLoader::OptionsL(RCommandOptionList& /*aOptions*/)
	{
	//TODO: aOptions.AppendBoolL(iOpt, _L("example_opt"));
	//TODO: Also remember to update the CIF file for any options you add.
	}

#define ToPtr(qstring) (const TUint16*)qstring.constData(), qstring.length()
#define FromDesc(tdesc) QString((const QChar*)tdesc.Ptr(), tdesc.Length())

void CCmdQPluginLoader::DoRunL()
	{
	// Slightly more likely not to crash while loading the plugin if we instantiate a QApplication?
	//int n = 0;
	//char* args = "\0";
	//QCoreApplication a(n, &args);

	iLoader.setFileName(QDir::fromNativeSeparators(FromDesc(iFileName)));
	Printf(_L("Loading %S...\r\n"), &iFileName);
	TBool loaded = EFalse;
	QT_TRYCATCH_LEAVING(iLoader.load());
	if (!loaded)
		{
		QString errString = iLoader.errorString();
		TPtrC errPtr(ToPtr(errString));
		LeaveIfErr(KErrUnknown, _L("Failed to load %S: %S"), &iFileName, &errPtr);
		}
	Printf(_L("Loaded ok. Instantiating root object...\r\n"));

	QObject* instance = iLoader.instance();
	if (!instance) LeaveIfErr(KErrUnknown, _L("Failed to instantiate root object"));
	QString objName = instance->objectName();
	TPtrC objPtr(ToPtr(objName));
	Printf(_L("Loaded object %S\r\n"), &objPtr);

	const QMetaObject* meta = instance->metaObject();
	Printf(_L8("Object class: %s\r\n"), meta->className());
	}
