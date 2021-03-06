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

#include "kcodecactionext.h"

#include <QVariant>
#include <QTextCodec>
#include <QMenu>
#include <QDebug>

#include <KCharsets>
#include <KActionCollection>
#include <KEncodingProber>
#include <KLocalizedString>

#include "../application.h"

void
KCodecActionExt::init(bool showDefault)
{
	m_autodetectAction = new QAction(parent());
	m_autodetectAction->setText(i18n("Autodetect"));
	m_autodetectAction->setCheckable(true);
	SubtitleComposer::app()->mainWindow()->actionCollection()->setShortcutsConfigurable(m_autodetectAction, false);
	m_autodetectAction->setData(QVariant((uint)KEncodingProber::Universal));
	m_autodetectAction->setActionGroup(selectableActionGroup());
	menu()->insertAction(action(0), m_autodetectAction);
	menu()->insertSeparator(action(1));

	bool encodingFound;
	foreach(QAction * action, actions()) {
		KSelectAction *groupAction = qobject_cast<KSelectAction *>(action);
		if(groupAction) {
			foreach(QAction * subAction, groupAction->actions()) {
				if(subAction->data().isNull()) {
					QTextCodec *codec = KCharsets::charsets()->codecForName(subAction->text().remove("&"), encodingFound);
					subAction->setText(encodingFound ? QString(codec->name()).toUpper() : subAction->text().toUpper());
				}
			}
		}
	}

	m_showDefault = showDefault;
	if(!m_showDefault)
		menu()->removeAction(this->action(0));
}

KCodecActionExt::KCodecActionExt(QObject *parent, bool showAutoOptions, bool showDefault) :
	KCodecAction(parent, showAutoOptions)
{
	init(showDefault);
}

KCodecActionExt::KCodecActionExt(const QString &text, QObject *parent, bool showAutoOptions, bool showDefault) :
	KCodecAction(text, parent, showAutoOptions)
{
	init(showDefault);
}

KCodecActionExt::KCodecActionExt(const QIcon &icon, const QString &text, QObject *parent, bool showAutoOptions, bool showDefault) :
	KCodecAction(icon, text, parent, showAutoOptions)
{
	init(showDefault);
}

void
KCodecActionExt::actionTriggered(QAction *action)
{
	// we don't want to emit any signals from top-level items
	// except for the default and autodetect ones
	if(action == m_autodetectAction) {
		emit triggered(KEncodingProber::Universal);
	} else if(m_showDefault && action == this->action(0)) {
		emit defaultItemTriggered();
	}
}


