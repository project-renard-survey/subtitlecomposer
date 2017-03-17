#ifndef VOBSUBINPUTFORMAT_H
#define VOBSUBINPUTFORMAT_H

/**
 * Copyright (C) 2007-2009 Sergio Pistone <sergio_pistone@yahoo.com.ar>
 * Copyright (C) 2010-2017 Mladen Milinkovic <max@smoothware.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "mplayer/mp_msg.h"
#include "mplayer/vobsub.h"
#include "mplayer/spudec.h"

#include "application.h"
#include "formats/inputformat.h"
#include "vobsubinputinitdialog.h"
#include "vobsubinputprocessdialog.h"

#include <QUrl>
#include <QRegExp>

namespace SubtitleComposer {
class VobSubInputFormat : public InputFormat
{
	friend class FormatManager;

public:
	virtual bool readBinary(Subtitle &subtitle, const QUrl &url)
	{
#if defined(VERBOSE) || !defined(NDEBUG)
		qputenv("MPLAYER_VERBOSE", QByteArrayLiteral("1"));
#endif

		mp_msg_init();

		const QString filename = url.toLocalFile();
		const QByteArray filebase = filename.left(filename.lastIndexOf('.')).toLatin1();

		// Open the sub/idx subtitles
		void *spu;
		void *vob = vobsub_open(filebase.constData(), 0, 1, 0, &spu);
		if(!vob || !vobsub_get_indexes_count(vob)) {
			qDebug() << "Couldn't open VobSub files '" << filebase << ".idx/.sub'\n";
			return false;
		}

		VobSubInputInitDialog dlgInit(vob, spu, Application::instance()->mainWindow());
		if(dlgInit.exec() == QDialog::Rejected)
			return true;

		vobsub_id = dlgInit.streamIndex();

		// show subtitle updates in realtime
		LinesWidget *linesWidget = Application::instance()->linesWidget();
		Subtitle *oldSubtitle = linesWidget->model()->subtitle();
		linesWidget->setSubtitle(&subtitle);

		VobSubInputProcessDialog dlgProc(&subtitle, vob, spu, Application::instance()->mainWindow());
		dlgProc.exec();

		// restore original subtitle
		linesWidget->setSubtitle(oldSubtitle);

		return true;
	}

protected:
	virtual bool parseSubtitles(Subtitle &, const QString &) const
	{
		return false;
	}

	VobSubInputFormat()
		: InputFormat(QStringLiteral("VobSub"),
		  QStringList(QStringLiteral("idx"))),
		  m_regExp(QStringLiteral("[\\d]+\n([0-2][0-9]):([0-5][0-9]):([0-5][0-9])[,\\.]([0-9]+) --> ([0-2][0-9]):([0-5][0-9]):([0-5][0-9])[,\\.]([0-9]+)\n"))
	{}

	mutable QRegExp m_regExp;
	QUrl m_url;
};
}

#endif
