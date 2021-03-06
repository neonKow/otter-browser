/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2015 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
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

#include "QtWebEngineWebBackend.h"
#include "QtWebEngineWebWidget.h"
#include "../../../../core/NetworkManagerFactory.h"
#include "../../../../core/SettingsManager.h"
#include "../../../../core/Utils.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtWebEngineWidgets/QWebEngineProfile>
#include <QtWebEngineWidgets/QWebEngineSettings>

namespace Otter
{

QtWebEngineWebBackend::QtWebEngineWebBackend(QObject *parent) : WebBackend(parent),
	m_isInitialized(false)
{
}

void QtWebEngineWebBackend::optionChanged(const QString &option)
{
	if (!(option.startsWith(QLatin1String("Browser/")) || option.startsWith(QLatin1String("Content/"))))
	{
		return;
	}

	QWebEngineSettings *globalSettings = QWebEngineSettings::globalSettings();
	globalSettings->setAttribute(QWebEngineSettings::AutoLoadImages, SettingsManager::getValue(QLatin1String("Browser/EnableImages")).toBool());
	globalSettings->setAttribute(QWebEngineSettings::JavascriptEnabled, SettingsManager::getValue(QLatin1String("Browser/EnableJavaScript")).toBool());
	globalSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, SettingsManager::getValue(QLatin1String("Browser/JavaScriptCanAccessClipboard")).toBool());
	globalSettings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, SettingsManager::getValue(QLatin1String("Browser/JavaScriptCanOpenWindows")).toBool());
	globalSettings->setAttribute(QWebEngineSettings::LocalStorageEnabled, SettingsManager::getValue(QLatin1String("Browser/EnableLocalStorage")).toBool());
	globalSettings->setFontSize(QWebEngineSettings::DefaultFontSize, SettingsManager::getValue(QLatin1String("Content/DefaultFontSize")).toInt());
	globalSettings->setFontSize(QWebEngineSettings::DefaultFixedFontSize, SettingsManager::getValue(QLatin1String("Content/DefaultFixedFontSize")).toInt());
	globalSettings->setFontSize(QWebEngineSettings::MinimumFontSize, SettingsManager::getValue(QLatin1String("Content/MinimumFontSize")).toInt());
	globalSettings->setFontFamily(QWebEngineSettings::StandardFont, SettingsManager::getValue(QLatin1String("Content/StandardFont")).toString());
	globalSettings->setFontFamily(QWebEngineSettings::FixedFont, SettingsManager::getValue(QLatin1String("Content/FixedFont")).toString());
	globalSettings->setFontFamily(QWebEngineSettings::SerifFont, SettingsManager::getValue(QLatin1String("Content/SerifFont")).toString());
	globalSettings->setFontFamily(QWebEngineSettings::SansSerifFont, SettingsManager::getValue(QLatin1String("Content/SansSerifFont")).toString());
	globalSettings->setFontFamily(QWebEngineSettings::CursiveFont, SettingsManager::getValue(QLatin1String("Content/CursiveFont")).toString());
	globalSettings->setFontFamily(QWebEngineSettings::FantasyFont, SettingsManager::getValue(QLatin1String("Content/FantasyFont")).toString());
}

WebWidget* QtWebEngineWebBackend::createWidget(bool isPrivate, ContentsWidget *parent)
{
	if (!m_isInitialized)
	{
		m_isInitialized = true;

		const QString cachePath = SessionsManager::getCachePath();

		if (cachePath.isEmpty())
		{
			QWebEngineProfile::defaultProfile()->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
		}
		else
		{
			QDir().mkpath(cachePath);

			QWebEngineProfile::defaultProfile()->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
			QWebEngineProfile::defaultProfile()->setHttpCacheMaximumSize(SettingsManager::getValue(QLatin1String("Cache/DiskCacheLimit")).toInt() * 1024);
			QWebEngineProfile::defaultProfile()->setCachePath(cachePath);
			QWebEngineProfile::defaultProfile()->setPersistentStoragePath(cachePath  + QLatin1String("/persistentStorage/"));
		}

		optionChanged(QLatin1String("Browser/"));

		connect(SettingsManager::getInstance(), SIGNAL(valueChanged(QString,QVariant)), this, SLOT(optionChanged(QString)));
	}

	return new QtWebEngineWebWidget(isPrivate, this, parent);
}

QString QtWebEngineWebBackend::getTitle() const
{
	return tr("Blink Backend");
}

QString QtWebEngineWebBackend::getDescription() const
{
	return tr("Backend utilizing QtWebEngine module");
}

QString QtWebEngineWebBackend::getVersion() const
{
	return QCoreApplication::applicationVersion();
}

QString QtWebEngineWebBackend::getEngineVersion() const
{
	return QLatin1String("37.0");
}

QString QtWebEngineWebBackend::getUserAgent(const QString &pattern) const
{
	return pattern;
}

QUrl QtWebEngineWebBackend::getHomePage() const
{
	return QUrl(QLatin1String("http://otter-browser.org/"));
}

QIcon QtWebEngineWebBackend::getIcon() const
{
	return QIcon();
}

QIcon QtWebEngineWebBackend::getIconForUrl(const QUrl &url)
{
	Q_UNUSED(url)

	return Utils::getIcon(QLatin1String("text-html"));
}

}
