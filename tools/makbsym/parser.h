// parser.h
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

#ifndef MB_PARSER_H
#define MB_PARSER_H

#include <QString>
#include <QByteArray>

namespace Parser
	{
	QString ShortProcessName(const QString& aName);
	QString ShortThreadName(const QString& aName);
	QString ShortWindowgroupName(const QString& aName);
	QString Uint(unsigned int x); // returns decimal if less than 1000, otherwise hex with preceding 0x
	QString Int(int x); // returns decimal if less than 1000 or negative, otherwise hex with preceding 0x
	QString MemInt(qint64 x); // Returns a number in MB, GB etc as appropriate
	QString HexDumpLE(const QByteArray& aData);
	QString HexDump(const QByteArray& aData);
	QString HtmlEscape(const QString& aString);
	};

#endif // MB_PARSER_H
