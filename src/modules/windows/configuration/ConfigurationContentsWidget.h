/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2015 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#ifndef OTTER_CONFIGURATIONCONTENTSWIDGET_H
#define OTTER_CONFIGURATIONCONTENTSWIDGET_H

#include "../../../ui/ContentsWidget.h"

#include <QtGui/QStandardItemModel>

namespace Otter
{

namespace Ui
{
	class ConfigurationContentsWidget;
}

class Window;

class ConfigurationContentsWidget : public ContentsWidget
{
	Q_OBJECT

public:
	explicit ConfigurationContentsWidget(Window *window);
	~ConfigurationContentsWidget();

	void print(QPrinter *printer);
	QString getTitle() const;
	QLatin1String getType() const;
	QUrl getUrl() const;
	QIcon getIcon() const;

public slots:
	void triggerAction(int identifier, bool checked = false);

protected:
	void changeEvent(QEvent *event);

protected slots:
	void optionChanged(const QString &option, const QVariant &value);
	void currentChanged(const QModelIndex &currentIndex, const QModelIndex &previousIndex);
	void copyOptionName();
	void copyOptionValue();
	void restoreDefaults();
	void filterConfiguration(const QString &filter);
	void showContextMenu(const QPoint &point);

private:
	QStandardItemModel *m_model;
	Ui::ConfigurationContentsWidget *m_ui;
};

}

#endif
