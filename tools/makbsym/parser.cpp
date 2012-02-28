// parser.cpp
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

#include <QtEndian>
#include <QRegExp>
#include "parser.h"

static const QString KProcessRegex = "^(.*)\\[([0-9a-f]{8})\\]([0-9]{4})";
static const QString KThreadRegex = QString().append(KProcessRegex).append("::(.*)");

QString Parser::ShortProcessName(const QString& aName)
	{
	QRegExp proc(KProcessRegex);
	if (proc.indexIn(aName) < 0)
		{
		// Not a thread name
		return aName;
		}

	QString procname = proc.cap(1);
	int nonce = proc.cap(3).toInt();
	if (nonce > 1)
		{
		procname.append(QString("(%1)").arg(nonce));
		}
	return procname;
	}

QString Parser::ShortThreadName(const QString& aName)
	{
	QRegExp threadRegex(KThreadRegex, Qt::CaseInsensitive);
	if (threadRegex.indexIn(aName) < 0)
		{
		// Not a thread name
		return aName;
		}

	QString procname = threadRegex.cap(1);
	if (procname.endsWith(".exe", Qt::CaseInsensitive)) procname.chop(4);
	//qDebug(qPrintable(procname));
	QString threadname = threadRegex.cap(4);
	int nonce = threadRegex.cap(3).toInt();

	QString nonceString;
	if (nonce > 1)
		{
		nonceString = QString("(%1)").arg(nonce);
		}

	QString res;
	// Optimise away names whether the thread short name matches the process short name
	if (procname.compare(threadname, Qt::CaseInsensitive) == 0 || (threadname.startsWith("!") && procname.compare(threadname.mid(1), Qt::CaseInsensitive) == 0))
		{
		res.append(threadname).append(nonceString);
		}
	else
		{
		res.append(procname).append(nonceString).append("::").append(threadname);
		}

	return res;
	}

QString Parser::Int(int x)
	{
	if (x < 1024)
		{
		return QString::number(x);
		}
	else
		{
		return Uint(x);
		}
	}

QString Parser::Uint(unsigned int x)
	{
	if (x < 1024)
		{
		return QString::number(x);
		}
	else
		{
		return QString("0x%1").arg(x, 0, 16);
		}
	}

QString Parser::ShortWindowgroupName(const QString& aName)
	{
	QRegExp regex(QString("^[^.]{2}\\.[^.]*\\.([^.]*)\\."), Qt::CaseInsensitive);
	if (regex.indexIn(aName) < 0)
		{
		// Not a valid wg name (most likely, "<untitled window group>" or somesuch
		return aName;
		}

	QString name = regex.cap(1);
	return name;
	}

#define KB *1024
#define MB KB*1024
#define GB MB*1024
#define TB GB*Q_INT64_C(1024) // The Q_INT64_C is needed to promote the maths to 64-bit

QString Parser::MemInt(qint64 aSize)
	{
	static const QString KBytes = " B";
	static const QString KKilobytes =" KB";
	static const QString KMegabytes = " MB";
	static const QString KGigabytes = " GB";
	static const QString KTerabytes = " TB";

	const QString* suff = &KBytes;
	double n = aSize;
	qint64 factor = 1;

	qint64 absSize = aSize;
	if (absSize < 0) absSize = -absSize;

	if (absSize >= 1 TB)
		{
		suff = &KTerabytes;
		factor = 1 TB;
		}
	else if (absSize >= 1 GB)
		{
		suff = &KGigabytes;
		factor = 1 GB;
		}
	else if (absSize >= 1 MB)
		{
		suff = &KMegabytes;
		factor = 1 MB;
		}
	else if (absSize >= 1 KB)
		{
		suff = &KKilobytes;
		factor = 1 KB;
		}

	n = n / (double)factor;
	bool wholeNumUnits = (absSize & (factor-1)) == 0; // ie aSize % factor == 0

	QString result = QString("%1%2").arg(n, 0, 'f', wholeNumUnits ? 0 : 2).arg(*suff);
	return result;
	}

QString Parser::HexDumpLE(const QByteArray& aData)
	{
	const quint32* ptr = (const quint32*)aData.constData();
	QString result;

	for (int i = 0; i*4 < aData.count(); i++)
		{
		quint32 word = qFromLittleEndian(ptr[i]);
		result.append(QString("%1 ").arg(word, 8, 16, QLatin1Char('0')));
		}

	QByteArray remainder = aData.mid(aData.count() & (~3));
	if (remainder.count())
		{
		result.append(QString(remainder.toHex()));
		}
	return result;
	}

QString Parser::HexDump(const QByteArray& aData)
	{
	const uchar* ptr = (const uchar*)aData.constData();
	QString result;
	for (int i = 0; i < aData.count(); i++)
		{
		result.append(QString("%1 ").arg(ptr[i], 2, 16, QLatin1Char('0')));
		}
	return result;
	}

QString Parser::HtmlEscape(const QString& aString)
	{
	QString result(aString);
	result.replace("&", "&amp;");
	result.replace("<", "&lt;");
	result.replace(">", "&gt;");
	return result;
	}
