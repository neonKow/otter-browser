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

#include "PreferencesSearchPageWidget.h"
#include "SearchKeywordDelegate.h"
#include "../OptionDelegate.h"
#include "../SearchDelegate.h"
#include "../SearchPropertiesDialog.h"
#include "../../core/SearchesManager.h"
#include "../../core/SessionsManager.h"
#include "../../core/SettingsManager.h"
#include "../../core/Utils.h"

#include "ui_PreferencesSearchPageWidget.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>

namespace Otter
{

PreferencesSearchPageWidget::PreferencesSearchPageWidget(QWidget *parent) : QWidget(parent),
	m_defaultSearchEngine(SettingsManager::getValue(QLatin1String("Search/DefaultSearchEngine")).toString()),
	m_ui(new Ui::PreferencesSearchPageWidget)
{
	m_ui->setupUi(this);
	QStringList searchEnginesLabels;
	searchEnginesLabels << tr("Name") << tr("Keyword");

	QStandardItemModel *searchEnginesModel = new QStandardItemModel(this);
	searchEnginesModel->setHorizontalHeaderLabels(searchEnginesLabels);

	const QStringList searchEngines = SearchesManager::getSearchEngines();

	for (int i = 0; i < searchEngines.count(); ++i)
	{
		const SearchInformation engine = SearchesManager::getSearchEngine(searchEngines.at(i));

		if (engine.identifier.isEmpty())
		{
			continue;
		}

		m_searchEngines[engine.identifier] = qMakePair(false, engine);

		QList<QStandardItem*> items;
		items.append(new QStandardItem(engine.icon, engine.title));
		items[0]->setToolTip(engine.description);
		items[0]->setData(engine.identifier, Qt::UserRole);
		items[0]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
		items.append(new QStandardItem(engine.keyword));
		items[1]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

		searchEnginesModel->appendRow(items);
	}

	m_ui->searchViewWidget->setModel(searchEnginesModel);
	m_ui->searchViewWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_ui->searchViewWidget->setItemDelegateForColumn(0, new OptionDelegate(true, this));
	m_ui->searchViewWidget->setItemDelegateForColumn(1, new SearchKeywordDelegate(this));
	m_ui->searchSuggestionsCheckBox->setChecked(SettingsManager::getValue(QLatin1String("Search/SearchEnginesSuggestions")).toBool());

	QMenu *addSearchMenu = new QMenu(m_ui->addSearchButton);
	addSearchMenu->addAction(tr("New…"));
	addSearchMenu->addAction(tr("Readd"))->setMenu(new QMenu(m_ui->addSearchButton));

	m_ui->addSearchButton->setMenu(addSearchMenu);
	m_ui->moveDownSearchButton->setIcon(Utils::getIcon(QLatin1String("arrow-down")));
	m_ui->moveUpSearchButton->setIcon(Utils::getIcon(QLatin1String("arrow-up")));

	updateReaddSearchMenu();

	connect(m_ui->searchFilterLineEdit, SIGNAL(textChanged(QString)), m_ui->searchViewWidget, SLOT(setFilter(QString)));
	connect(m_ui->searchViewWidget, SIGNAL(canMoveDownChanged(bool)), m_ui->moveDownSearchButton, SLOT(setEnabled(bool)));
	connect(m_ui->searchViewWidget, SIGNAL(canMoveUpChanged(bool)), m_ui->moveUpSearchButton, SLOT(setEnabled(bool)));
	connect(m_ui->searchViewWidget, SIGNAL(needsActionsUpdate()), this, SLOT(updateSearchActions()));
	connect(m_ui->searchViewWidget, SIGNAL(modified()), this, SIGNAL(settingsModified()));
	connect(m_ui->addSearchButton->menu()->actions().at(0), SIGNAL(triggered()), this, SLOT(addSearchEngine()));
	connect(m_ui->addSearchButton->menu()->actions().at(1)->menu(), SIGNAL(triggered(QAction*)), this, SLOT(readdSearchEngine(QAction*)));
	connect(m_ui->editSearchButton, SIGNAL(clicked()), this, SLOT(editSearchEngine()));
	connect(m_ui->removeSearchButton, SIGNAL(clicked()), this, SLOT(removeSearchEngine()));
	connect(m_ui->moveDownSearchButton, SIGNAL(clicked()), m_ui->searchViewWidget, SLOT(moveDownRow()));
	connect(m_ui->moveUpSearchButton, SIGNAL(clicked()), m_ui->searchViewWidget, SLOT(moveUpRow()));
}

PreferencesSearchPageWidget::~PreferencesSearchPageWidget()
{
	delete m_ui;
}

void PreferencesSearchPageWidget::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);

	switch (event->type())
	{
		case QEvent::LanguageChange:
			m_ui->retranslateUi(this);

			break;
		default:
			break;
	}
}

void PreferencesSearchPageWidget::addSearchEngine()
{
	QStringList identifiers;
	QStringList keywords;

	for (int i = 0; i < m_ui->searchViewWidget->getRowCount(); ++i)
	{
		identifiers.append(m_ui->searchViewWidget->getIndex(i, 0).data(Qt::UserRole).toHash().value(QLatin1String("identifier"), QString()).toString());

		const QString keyword = m_ui->searchViewWidget->getIndex(i, 1).data(Qt::DisplayRole).toString();

		if (!keyword.isEmpty())
		{
			keywords.append(keyword);
		}
	}

	const QString identifier = Utils::createIdentifier(QString(), identifiers);

	if (identifier.isEmpty())
	{
		return;
	}

	SearchInformation engine;
	engine.identifier = identifier;
	engine.title = tr("New Search Engine");
	engine.icon = Utils::getIcon(QLatin1String("edit-find"));

	SearchPropertiesDialog dialog(engine, keywords, false, this);

	if (dialog.exec() == QDialog::Rejected)
	{
		return;
	}

	engine = dialog.getSearchEngine();

	m_searchEngines[identifier] = qMakePair(true, engine);

	if (dialog.isDefault())
	{
		m_defaultSearchEngine = identifier;
	}

	QList<QStandardItem*> items;
	items.append(new QStandardItem(engine.icon, engine.title));
	items[0]->setToolTip(engine.description);
	items[0]->setData(identifier, Qt::UserRole);
	items[0]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	items.append(new QStandardItem(engine.keyword));
	items[1]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

	m_ui->searchViewWidget->insertRow(items);

	emit settingsModified();
}

void PreferencesSearchPageWidget::readdSearchEngine(QAction *action)
{
	if (!action || action->data().isNull())
	{
		return;
	}

	const QString identifier = action->data().toString();
	QFile file(SessionsManager::getReadableDataPath(QLatin1String("searches/") + identifier + QLatin1String(".xml")));

	if (!file.open(QIODevice::ReadOnly))
	{
		return;
	}

	const SearchInformation engine = SearchesManager::loadSearchEngine(&file, identifier);

	file.close();

	if (engine.identifier.isEmpty() || m_searchEngines.contains(identifier))
	{
		return;
	}

	m_searchEngines[identifier] = qMakePair(false, engine);

	QList<QStandardItem*> items;
	items.append(new QStandardItem(engine.icon, engine.title));
	items[0]->setToolTip(engine.description);
	items[0]->setData(identifier, Qt::UserRole);
	items[0]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	items.append(new QStandardItem(engine.keyword));
	items[1]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

	m_ui->searchViewWidget->insertRow(items);

	updateReaddSearchMenu();

	emit settingsModified();
}

void PreferencesSearchPageWidget::editSearchEngine()
{
	const QModelIndex index = m_ui->searchViewWidget->getIndex(m_ui->searchViewWidget->getCurrentRow(), 0);
	const QString identifier = index.data(Qt::UserRole).toString();

	if (identifier.isEmpty() || !m_searchEngines.contains(identifier))
	{
		return;
	}

	QStringList keywords;

	for (int i = 0; i < m_ui->searchViewWidget->getRowCount(); ++i)
	{
		const QString keyword = m_ui->searchViewWidget->getIndex(i, 1).data(Qt::DisplayRole).toString();

		if (m_ui->searchViewWidget->getCurrentRow() != i && !keyword.isEmpty())
		{
			keywords.append(keyword);
		}
	}

	SearchPropertiesDialog dialog(m_searchEngines[identifier].second, keywords, (identifier == m_defaultSearchEngine), this);

	if (dialog.exec() == QDialog::Rejected)
	{
		return;
	}

	SearchInformation engine = dialog.getSearchEngine();

	if (keywords.contains(engine.keyword))
	{
		engine.keyword = QString();
	}

	m_searchEngines[identifier] = qMakePair(true, engine);

	if (dialog.isDefault())
	{
		m_defaultSearchEngine = identifier;
	}

	m_ui->searchViewWidget->setData(index, engine.title, Qt::DisplayRole);
	m_ui->searchViewWidget->setData(index, engine.description, Qt::ToolTipRole);
	m_ui->searchViewWidget->setData(index, engine.icon, Qt::DecorationRole);
	m_ui->searchViewWidget->setData(m_ui->searchViewWidget->getIndex(index.row(), 1), engine.keyword, Qt::DisplayRole);

	emit settingsModified();
}

void PreferencesSearchPageWidget::removeSearchEngine()
{
	const QString identifier = m_ui->searchViewWidget->getIndex(m_ui->searchViewWidget->getCurrentRow(), 0).data(Qt::UserRole).toString();

	if (identifier.isEmpty() || !m_searchEngines.contains(identifier))
	{
		return;
	}

	QMessageBox messageBox;
	messageBox.setWindowTitle(tr("Question"));
	messageBox.setText(tr("Do you really want to remove this search engine?"));
	messageBox.setIcon(QMessageBox::Question);
	messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
	messageBox.setDefaultButton(QMessageBox::Cancel);

	const QString path = SessionsManager::getWritableDataPath(QLatin1String("searches/") + identifier + QLatin1String(".xml"));

	if (QFile::exists(path))
	{
		messageBox.setCheckBox(new QCheckBox(tr("Delete search engine permanently")));
	}

	if (messageBox.exec() == QMessageBox::Yes)
	{
		if (messageBox.checkBox() && messageBox.checkBox()->isChecked())
		{
			m_filesToRemove.append(path);
		}

		m_searchEngines.remove(identifier);

		m_ui->searchViewWidget->removeRow();

		updateReaddSearchMenu();

		emit settingsModified();
	}
}

void PreferencesSearchPageWidget::updateSearchActions()
{
	const int currentRow = m_ui->searchViewWidget->getCurrentRow();
	const bool isSelected = (currentRow >= 0 && currentRow < m_ui->searchViewWidget->getRowCount());

	m_ui->editSearchButton->setEnabled(isSelected);
	m_ui->removeSearchButton->setEnabled(isSelected);
}

void PreferencesSearchPageWidget::updateReaddSearchMenu()
{
	if (!m_ui->addSearchButton->menu())
	{
		return;
	}

	QStringList availableIdentifiers;
	QList<SearchInformation> availableSearchEngines;
	QList<QFileInfo> allSearchEngines = QDir(SessionsManager::getReadableDataPath(QLatin1String("searches"))).entryInfoList(QDir::Files);
	allSearchEngines.append(QDir(SessionsManager::getReadableDataPath(QLatin1String("searches"), true)).entryInfoList(QDir::Files));

	for (int i = 0; i < allSearchEngines.count(); ++i)
	{
		const QString identifier = allSearchEngines.at(i).baseName();

		if (!m_searchEngines.contains(identifier) && !availableIdentifiers.contains(identifier))
		{
			QFile file(allSearchEngines.at(i).absoluteFilePath());

			if (file.open(QFile::ReadOnly))
			{
				const SearchInformation engine = SearchesManager::loadSearchEngine(&file, identifier);

				if (!engine.identifier.isEmpty())
				{
					availableIdentifiers.append(identifier);

					availableSearchEngines.append(engine);
				}

				file.close();
			}
		}
	}

	m_ui->addSearchButton->menu()->actions().at(1)->menu()->clear();
	m_ui->addSearchButton->menu()->actions().at(1)->menu()->setEnabled(!availableSearchEngines.isEmpty());

	for (int i = 0; i < availableSearchEngines.count(); ++i)
	{
		m_ui->addSearchButton->menu()->actions().at(1)->menu()->addAction(availableSearchEngines.at(i).icon, (availableSearchEngines.at(i).title.isEmpty() ? tr("(Untitled)") : availableSearchEngines.at(i).title))->setData(availableSearchEngines.at(i).identifier);
	}
}

void PreferencesSearchPageWidget::save()
{
	for (int i = 0; i < m_filesToRemove.count(); ++i)
	{
		QFile::remove(m_filesToRemove.at(i));
	}

	m_filesToRemove.clear();

	QStringList searchEnginesOrder;

	for (int i = 0; i < m_ui->searchViewWidget->getRowCount(); ++i)
	{
		const QString identifier = m_ui->searchViewWidget->getIndex(i, 0).data(Qt::UserRole).toString();
		const QString keyword = m_ui->searchViewWidget->getIndex(i, 1).data(Qt::DisplayRole).toString();

		if (!identifier.isEmpty())
		{
			searchEnginesOrder.append(identifier);
		}

		if (m_searchEngines.contains(identifier) && m_searchEngines[identifier].second.keyword != keyword)
		{
			m_searchEngines[identifier].first = true;
			m_searchEngines[identifier].second.keyword = keyword;
		}
	}

	QDir().mkpath(SessionsManager::getWritableDataPath(QLatin1String("searches")));

	QHash<QString, QPair<bool, SearchInformation> >::iterator searchEnginesIterator;

	for (searchEnginesIterator = m_searchEngines.begin(); searchEnginesIterator != m_searchEngines.end(); ++searchEnginesIterator)
	{
		if (searchEnginesIterator.value().first)
		{
			SearchesManager::saveSearchEngine(searchEnginesIterator.value().second);
		}
	}

	if (SettingsManager::getValue(QLatin1String("Search/SearchEnginesOrder")).toStringList() == searchEnginesOrder)
	{
		SearchesManager::loadSearchEngines();
	}
	else
	{
		SettingsManager::setValue(QLatin1String("Search/SearchEnginesOrder"), searchEnginesOrder);
	}

	SettingsManager::setValue(QLatin1String("Search/DefaultSearchEngine"), m_defaultSearchEngine);
	SettingsManager::setValue(QLatin1String("Search/SearchEnginesSuggestions"), m_ui->searchSuggestionsCheckBox->isChecked());

	updateReaddSearchMenu();
}

}
