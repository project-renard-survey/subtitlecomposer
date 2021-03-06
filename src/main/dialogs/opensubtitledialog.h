#ifndef OPENSUBTITLEDIALOG_H
#define OPENSUBTITLEDIALOG_H

/**
 * Copyright (C) 2007-2009 Sergio Pistone <sergio_pistone@yahoo.com.ar>
 * Copyright (C) 2010-2015 Mladen Milinkovic <max@smoothware.net>
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

#include "kfilewidget.h"

#include <QDialog>
#include <QUrl>

QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QDialogButtonBox)

namespace SubtitleComposer {
class OpenSubtitleDialog : public QDialog
{
	Q_OBJECT

public:
	explicit OpenSubtitleDialog(bool primary, const QUrl &startDir = QUrl(), const QString &encoding = QString(), QWidget *parent = 0);

	QString selectedEncoding() const;

	static QString inputFormatsFilter();

	QUrl selectedUrl() const;

protected:
	virtual void showEvent(QShowEvent *event);
	virtual void hideEvent(QHideEvent *event);

private:
	QComboBox *m_encodingComboBox;
	KFileWidget *m_fileWidget;
	QDialogButtonBox *m_buttons;
};
}
#endif
