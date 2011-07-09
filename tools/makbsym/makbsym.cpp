// makbsym.cpp
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

#include <QtCore>
#include "makbsym.h"
#include "symbolics.h"

void LookupMode(const QString& aBsym, const QStringList& aAddresses);

int main(int argc, char **argv)
	{
    QCoreApplication app(argc, argv);
	QStringList args = QCoreApplication::arguments();
	if (args.count() < 2)
		{
		qWarning("Syntax: makbsym <file> [<file>...] [output.bsym]");
		qWarning("            Where <file> is *.symbol, rombuild.log, rofsbuild.log, mapfile.map");
		qWarning("        makbsym file.bsym  --lookup <address> [, <address>, ...]");
		return 1;
		}
	
	QStringList inputs = args.mid(1, args.length()-1);

    if (inputs.count() > 1 && inputs[1] == "--lookup")
		{
		QString file = inputs[0];
		inputs.removeAt(0);
		inputs.removeAll("--lookup");
		LookupMode(file, inputs);
		return 0;
		}

	QFileInfo inf(inputs[0]);
	QString output = inf.path().append("/").append(inf.completeBaseName()).append(".bsym");
	if (inputs.count() > 1 && inputs.last().endsWith(".bsym"))
		{
		output = inputs.last();
		inputs.removeLast();
		}
	CSymbolics* symbolics = new CSymbolics(false); // False to not auto-create bsyms, we want finer grained control
	TProgressPrinter progressObj;
	QObject::connect(symbolics, SIGNAL(Progress(int)), &progressObj, SLOT(Progress(int)));
	QObject::connect(symbolics, SIGNAL(SetProgressMaximum(int)), &progressObj, SLOT(SetProgressMaximum(int)));

	foreach (QString input, inputs)
		{
		if (input.endsWith(".symbol", Qt::CaseInsensitive))
			{
			bool ok = symbolics->AddSymbolFile(input);
			if (!ok)
				{
				qWarning("Failed to load symbol file %s", qPrintable(input));
				return 3;
				}
			}
		else if (input.endsWith(".log", Qt::CaseInsensitive))
			{
			bool ok = symbolics->AddRombuildLogFile(input);
			if (!ok)
				{
				qWarning("Failed to parse log file %s", qPrintable(input));
				return 5;
				}
			}
		else if (input.endsWith(".map", Qt::CaseInsensitive))
			{
			bool ok = symbolics->AddMapFile(input);
			if (!ok)
				{
				qWarning("Failed to parse map file %s", qPrintable(input));
				return 5;
				}
			}
		else
			{
			qWarning("Error: currently only .symbol, .log or .map files are supported.");
			return 2;
			}
		}
	
	//qDebug("Writing to file %s", qPrintable(output));
	int symbolCount = symbolics->WriteBsymFile(output);
	if (symbolCount == 0)
		{
		qWarning("Couldn't write output bsym file to %s", qPrintable(QDir::toNativeSeparators(output)));
		return 4;
		}
	qDebug("Wrote %d symbols to %s.", symbolCount, qPrintable(QDir::toNativeSeparators(output)));

	return 0;
	}

void TProgressPrinter::SetProgressMaximum(int aMax)
	{
	iMax = aMax;
	}

void TProgressPrinter::Progress(int aProgress)
	{
	// No QT fn for printing to the standard io streams?
	fprintf(stderr, ".");
	if (aProgress == iMax) fprintf(stderr, "\n");
	}

void LookupMode(const QString& aBsym, const QStringList& aAddresses)
	{
	QScopedPointer<CSymbolics> sym(new CSymbolics(false));
	bool ok = sym->AddBsymFile(aBsym);
	if (!ok)
		{
		qCritical("Couldn't open %s", qPrintable(aBsym));
		return;
		}

	foreach (const QString& addressString, aAddresses)
		{
		uint address = addressString.toUInt(NULL, 0);
		TLookupResult res = sym->Lookup(address);
		if (res.Valid())
			{
			qDebug("%08x: %s", address, qPrintable(res.Description()));
			}
		else
			{
			qDebug("%08x: ?", address);
			}
		}
	}
