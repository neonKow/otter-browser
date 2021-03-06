/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2015 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2015 Jan Bajer aka bajasoft <jbajer@gmail.com>
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

#include "QtWebKitWebWidget.h"
#include "QtWebKitNetworkManager.h"
#include "QtWebKitPluginFactory.h"
#include "QtWebKitPluginWidget.h"
#include "QtWebKitPage.h"
#include "../../../../core/ActionsManager.h"
#include "../../../../core/BookmarksManager.h"
#include "../../../../core/Console.h"
#include "../../../../core/CookieJar.h"
#include "../../../../core/ContentBlockingManager.h"
#include "../../../../core/GesturesManager.h"
#include "../../../../core/HistoryManager.h"
#include "../../../../core/InputInterpreter.h"
#include "../../../../core/NetworkCache.h"
#include "../../../../core/NetworkManager.h"
#include "../../../../core/NetworkManagerFactory.h"
#include "../../../../core/NotesManager.h"
#include "../../../../core/SearchesManager.h"
#include "../../../../core/SessionsManager.h"
#include "../../../../core/SettingsManager.h"
#include "../../../../core/Transfer.h"
#include "../../../../core/TransfersManager.h"
#include "../../../../core/Utils.h"
#include "../../../../ui/ContentsDialog.h"
#include "../../../../ui/ContentsWidget.h"
#include "../../../../ui/ImagePropertiesDialog.h"
#include "../../../../ui/MainWindow.h"
#include "../../../../ui/SearchPropertiesDialog.h"
#include "../../../../ui/WebsitePreferencesDialog.h"

#include <QtCore/QDataStream>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QClipboard>
#include <QtGui/QImageWriter>
#include <QtPrintSupport/QPrintPreviewDialog>
#include <QtWebKit/QWebHistory>
#include <QtWebKit/QWebElement>
#include <QtWebKit/QtWebKitVersion>
#include <QtWebKitWidgets/QWebFrame>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>

namespace Otter
{

QtWebKitWebWidget::QtWebKitWebWidget(bool isPrivate, WebBackend *backend, QtWebKitNetworkManager *networkManager, ContentsWidget *parent) : WebWidget(isPrivate, backend, parent),
	m_webView(new QWebView(this)),
	m_page(NULL),
	m_pluginFactory(new QtWebKitPluginFactory(this)),
	m_inspector(NULL),
	m_inspectorCloseButton(NULL),
	m_networkManager(networkManager),
	m_splitter(new QSplitter(Qt::Vertical, this)),
	m_canLoadPlugins(false),
	m_ignoreContextMenu(false),
	m_ignoreContextMenuNextTime(false),
	m_isUsingRockerNavigation(false),
	m_isLoading(false),
	m_isTyped(false)
{
	m_splitter->addWidget(m_webView);
	m_splitter->setChildrenCollapsible(false);
	m_splitter->setContentsMargins(0, 0, 0, 0);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(m_splitter);
	layout->setContentsMargins(0, 0, 0, 0);

	setLayout(layout);
	setFocusPolicy(Qt::StrongFocus);

	if (m_networkManager)
	{
		m_networkManager->setWidget(this);
	}
	else
	{
		m_networkManager = new QtWebKitNetworkManager(isPrivate, NULL, this);
	}

	m_page = new QtWebKitPage(m_networkManager, this);
	m_page->setParent(m_webView);
	m_page->setPluginFactory(m_pluginFactory);

	m_webView->setPage(m_page);
	m_webView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_webView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_webView->settings()->setAttribute(QWebSettings::PrivateBrowsingEnabled, isPrivate);
	m_webView->installEventFilter(this);

	if (isPrivate)
	{
		m_webView->settings()->setIconDatabasePath(QString());
	}

	optionChanged(QLatin1String("Browser/JavaScriptCanShowStatusMessages"), SettingsManager::getValue(QLatin1String("Browser/JavaScriptCanShowStatusMessages")));
	optionChanged(QLatin1String("Content/BackgroundColor"), SettingsManager::getValue(QLatin1String("Content/BackgroundColor")));
	optionChanged(QLatin1String("History/BrowsingLimitAmountWindow"), SettingsManager::getValue(QLatin1String("History/BrowsingLimitAmountWindow")));
	updateEditActions();
	setZoom(SettingsManager::getValue(QLatin1String("Content/DefaultZoom")).toInt());

	connect(BookmarksManager::getModel(), SIGNAL(modelModified()), this, SLOT(updateBookmarkActions()));
	connect(SettingsManager::getInstance(), SIGNAL(valueChanged(QString,QVariant)), this, SLOT(optionChanged(QString,QVariant)));
	connect(m_page, SIGNAL(aboutToNavigate(QWebFrame*,QWebPage::NavigationType)), this, SLOT(navigating(QWebFrame*,QWebPage::NavigationType)));
	connect(m_page, SIGNAL(requestedNewWindow(WebWidget*,OpenHints)), this, SIGNAL(requestedNewWindow(WebWidget*,OpenHints)));
	connect(m_page, SIGNAL(saveFrameStateRequested(QWebFrame*,QWebHistoryItem*)), this, SLOT(saveState(QWebFrame*,QWebHistoryItem*)));
	connect(m_page, SIGNAL(restoreFrameStateRequested(QWebFrame*)), this, SLOT(restoreState(QWebFrame*)));
	connect(m_page, SIGNAL(downloadRequested(QNetworkRequest)), this, SLOT(downloadFile(QNetworkRequest)));
	connect(m_page, SIGNAL(unsupportedContent(QNetworkReply*)), this, SLOT(downloadFile(QNetworkReply*)));
	connect(m_page, SIGNAL(linkHovered(QString,QString,QString)), this, SLOT(linkHovered(QString)));
	connect(m_page, SIGNAL(microFocusChanged()), this, SLOT(updateEditActions()));
	connect(m_page, SIGNAL(printRequested(QWebFrame*)), this, SLOT(handlePrintRequest(QWebFrame*)));
	connect(m_page, SIGNAL(windowCloseRequested()), this, SLOT(handleWindowCloseRequest()));
	connect(m_page, SIGNAL(featurePermissionRequested(QWebFrame*,QWebPage::Feature)), this, SLOT(handlePermissionRequest(QWebFrame*,QWebPage::Feature)));
	connect(m_page, SIGNAL(featurePermissionRequestCanceled(QWebFrame*,QWebPage::Feature)), this, SLOT(handlePermissionCancel(QWebFrame*,QWebPage::Feature)));
	connect(m_page, SIGNAL(loadStarted()), this, SLOT(pageLoadStarted()));
	connect(m_page, SIGNAL(loadFinished(bool)), this, SLOT(pageLoadFinished()));
	connect(m_page->mainFrame(), SIGNAL(loadFinished(bool)), this, SLOT(pageLoadFinished()));
	connect(m_page->mainFrame(), SIGNAL(contentsSizeChanged(QSize)), this, SIGNAL(progressBarGeometryChanged()));
	connect(m_page->mainFrame(), SIGNAL(initialLayoutCompleted()), this, SIGNAL(progressBarGeometryChanged()));
	connect(m_webView, SIGNAL(titleChanged(const QString)), this, SLOT(notifyTitleChanged()));
	connect(m_webView, SIGNAL(urlChanged(const QUrl)), this, SLOT(notifyUrlChanged(const QUrl)));
	connect(m_webView, SIGNAL(iconChanged()), this, SLOT(notifyIconChanged()));
	connect(m_networkManager, SIGNAL(messageChanged(QString)), this, SIGNAL(loadMessageChanged(QString)));
	connect(m_networkManager, SIGNAL(statusChanged(int,int,qint64,qint64,qint64)), this, SIGNAL(loadStatusChanged(int,int,qint64,qint64,qint64)));
	connect(m_networkManager, SIGNAL(documentLoadProgressChanged(int)), this, SIGNAL(loadProgress(int)));
	connect(m_splitter, SIGNAL(splitterMoved(int,int)), this, SIGNAL(progressBarGeometryChanged()));
}

QtWebKitWebWidget::~QtWebKitWebWidget()
{
	m_webView->stop();
	m_webView->settings()->setAttribute(QWebSettings::JavascriptEnabled, false);
}

void QtWebKitWebWidget::focusInEvent(QFocusEvent *event)
{
	WebWidget::focusInEvent(event);

	m_webView->setFocus();

	if (m_inspector && m_inspector->isVisible())
	{
		m_inspectorCloseButton->raise();
	}
}

void QtWebKitWebWidget::mousePressEvent(QMouseEvent *event)
{
	WebWidget::mousePressEvent(event);

	if (getScrollMode() == MoveScroll)
	{
		triggerAction(ActionsManager::EndScrollAction);

		m_ignoreContextMenuNextTime = true;
	}
}

void QtWebKitWebWidget::search(const QString &query, const QString &engine)
{
	QNetworkRequest request;
	QNetworkAccessManager::Operation method;
	QByteArray body;

	if (SearchesManager::setupSearchQuery(query, engine, &request, &method, &body))
	{
		setRequestedUrl(request.url(), false, true);
		updateOptions(request.url());

		m_webView->page()->mainFrame()->load(request, method, body);
	}
}

void QtWebKitWebWidget::print(QPrinter *printer)
{
	m_webView->print(printer);
}

void QtWebKitWebWidget::optionChanged(const QString &option, const QVariant &value)
{
	if (option == QLatin1String("Browser/JavaScriptCanShowStatusMessages"))
	{
		disconnect(m_webView->page(), SIGNAL(statusBarMessage(QString)), this, SLOT(setStatusMessage(QString)));

		if (value.toBool() || SettingsManager::getValue(option, getUrl()).toBool())
		{
			connect(m_webView->page(), SIGNAL(statusBarMessage(QString)), this, SLOT(setStatusMessage(QString)));
		}
		else
		{
			setStatusMessage(QString());
		}
	}
	else if (option == QLatin1String("Content/BackgroundColor"))
	{
		QPalette palette = m_page->palette();
		palette.setColor(QPalette::Base, QColor(value.toString()));

		m_page->setPalette(palette);
	}
	else if (option == QLatin1String("History/BrowsingLimitAmountWindow"))
	{
		m_webView->page()->history()->setMaximumItemCount(value.toInt());
	}
}

void QtWebKitWebWidget::navigating(QWebFrame *frame, QWebPage::NavigationType type)
{
	if (frame == m_page->mainFrame() && type != QWebPage::NavigationTypeBackOrForward)
	{
		pageLoadStarted();
		handleHistory();

		if (type == QWebPage::NavigationTypeLinkClicked || type == QWebPage::NavigationTypeFormSubmitted)
		{
			m_isTyped = false;
		}
	}
}

void QtWebKitWebWidget::pageLoadStarted()
{
	m_canLoadPlugins = (getOption(QLatin1String("Browser/EnablePlugins"), getUrl()).toString() == QLatin1String("enabled"));
	m_isLoading = true;
	m_thumbnail = QPixmap();

	updateNavigationActions();
	setStatusMessage(QString());
	setStatusMessage(QString(), true);

	emit progressBarGeometryChanged();
	emit loadingChanged(true);
}

void QtWebKitWebWidget::pageLoadFinished()
{
	if (!m_isLoading)
	{
		return;
	}

	m_isLoading = false;
	m_thumbnail = QPixmap();

	m_networkManager->resetStatistics();

	updateNavigationActions();
	handleHistory();
	startReloadTimer();

	emit loadingChanged(false);
}

void QtWebKitWebWidget::downloadFile(const QNetworkRequest &request)
{
#if QTWEBKIT_VERSION >= 0x050200
	if ((!m_hitResult.imageUrl().isEmpty() && request.url() == m_hitResult.imageUrl()) || (!m_hitResult.mediaUrl().isEmpty() && request.url() == m_hitResult.mediaUrl()))
#else
	if (!m_hitResult.imageUrl().isEmpty() && request.url() == m_hitResult.imageUrl())
#endif
	{
		NetworkCache *cache = NetworkManagerFactory::getCache();

		if (cache && cache->metaData(request.url()).isValid())
		{
			QIODevice *device = cache->data(request.url());

			if (device && device->size() > 0)
			{
				const QString path = TransfersManager::getSavePath(request.url().fileName());

				if (path.isEmpty())
				{
					device->deleteLater();

					return;
				}

				QFile file(path);

				if (!file.open(QFile::WriteOnly))
				{
					QMessageBox::critical(SessionsManager::getActiveWindow(), tr("Error"), tr("Failed to open file for writing."), QMessageBox::Close);
				}

				file.write(device->readAll());
				file.close();

				device->deleteLater();

				return;
			}

			if (device)
			{
				device->deleteLater();
			}
		}
		else if (!m_hitResult.imageUrl().isEmpty() && m_hitResult.imageUrl().url().contains(QLatin1String(";base64,")))
		{
			const QString imageUrl = m_hitResult.imageUrl().url();
			const QString imageType = imageUrl.mid(11, (imageUrl.indexOf(QLatin1Char(';')) - 11));
			const QString path = TransfersManager::getSavePath(tr("file") + QLatin1Char('.') + imageType);

			if (path.isEmpty())
			{
				return;
			}

			QImageWriter writer(path);

			if (!writer.write(QImage::fromData(QByteArray::fromBase64(imageUrl.mid(imageUrl.indexOf(QLatin1String(";base64,")) + 7).toUtf8()), imageType.toStdString().c_str())))
			{
				Console::addMessage(tr("Failed to save image: %1").arg(writer.errorString()), OtherMessageCategory, ErrorMessageLevel, path);
			}

			return;
		}

		QNetworkRequest mutableRequest(request);
		mutableRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);

		Transfer *transfer = new Transfer(mutableRequest, QString(), false, this);

		if (transfer->getState() == Transfer::RunningState)
		{
			connect(transfer, SIGNAL(finished()), transfer, SLOT(deleteLater()));
		}
		else
		{
			transfer->deleteLater();
		}
	}
	else
	{
		TransfersManager::startTransfer(request, QString(), false, isPrivate());
	}
}

void QtWebKitWebWidget::downloadFile(QNetworkReply *reply)
{
	TransfersManager::startTransfer(reply, QString(), false, isPrivate());
}

void QtWebKitWebWidget::saveState(QWebFrame *frame, QWebHistoryItem *item)
{
	if (frame == m_webView->page()->mainFrame())
	{
		QVariantList data = m_webView->history()->currentItem().userData().toList();

		if (data.isEmpty() || data.length() < 3)
		{
			data.clear();
			data.append(0);
			data.append(getZoom());
			data.append(m_webView->page()->mainFrame()->scrollPosition());
		}
		else
		{
			data[ZoomEntryData] = getZoom();
			data[PositionEntryData] = m_webView->page()->mainFrame()->scrollPosition();
		}

		item->setUserData(data);
	}
}

void QtWebKitWebWidget::restoreState(QWebFrame *frame)
{
	if (frame == m_webView->page()->mainFrame())
	{
		setZoom(m_webView->history()->currentItem().userData().toList().value(ZoomEntryData, getZoom()).toInt());

		if (m_webView->page()->mainFrame()->scrollPosition() == QPoint(0, 0))
		{
			m_webView->page()->mainFrame()->setScrollPosition(m_webView->history()->currentItem().userData().toList().value(PositionEntryData).toPoint());
		}
	}
}

void QtWebKitWebWidget::hideInspector()
{
	triggerAction(ActionsManager::InspectPageAction, false);
}

void QtWebKitWebWidget::linkHovered(const QString &link)
{
	setStatusMessage(link, true);
}

void QtWebKitWebWidget::clearPluginToken()
{
	m_pluginToken = QString();
}

void QtWebKitWebWidget::openUrl(const QUrl &url, OpenHints hints)
{
	WebWidget *widget = clone(false);
	widget->setRequestedUrl(url);

	emit requestedNewWindow(widget, hints);
}

void QtWebKitWebWidget::openRequest(const QUrl &url, QNetworkAccessManager::Operation operation, QIODevice *outgoingData)
{
	m_formRequestUrl = url;
	m_formRequestOperation = operation;
	m_formRequestBody = (outgoingData ? outgoingData->readAll() : QByteArray());

	if (outgoingData)
	{
		outgoingData->close();
		outgoingData->deleteLater();
	}

	setRequestedUrl(m_formRequestUrl, false, true);
	updateOptions(m_formRequestUrl);

	QTimer::singleShot(50, this, SLOT(openFormRequest()));
}

void QtWebKitWebWidget::openFormRequest()
{
	m_webView->page()->mainFrame()->load(QNetworkRequest(m_formRequestUrl), m_formRequestOperation, m_formRequestBody);

	m_formRequestUrl = QUrl();
	m_formRequestBody = QByteArray();
}

void QtWebKitWebWidget::handlePrintRequest(QWebFrame *frame)
{
	QPrintPreviewDialog printPreviewDialog(this);
	printPreviewDialog.setWindowFlags(printPreviewDialog.windowFlags() | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint);
	printPreviewDialog.setWindowTitle(tr("Print Preview"));

	if (QApplication::activeWindow())
	{
		printPreviewDialog.resize(QApplication::activeWindow()->size());
	}

	connect(&printPreviewDialog, SIGNAL(paintRequested(QPrinter*)), frame, SLOT(print(QPrinter*)));

	printPreviewDialog.exec();
}

void QtWebKitWebWidget::handleHistory()
{
	if (isPrivate() || m_page->history()->count() == 0)
	{
		return;
	}

	const QUrl url = getUrl();
	const qint64 identifier = m_page->history()->currentItem().userData().toList().value(IdentifierEntryData).toLongLong();

	if (identifier == 0)
	{
		QVariantList data;
		data.append(HistoryManager::addEntry(url, getTitle(), m_webView->icon(), m_isTyped));
		data.append(getZoom());
		data.append(QPoint(0, 0));

		m_page->history()->currentItem().setUserData(data);

		SessionsManager::markSessionModified();
		BookmarksManager::updateVisits(url.toString());
	}
	else if (identifier > 0)
	{
		HistoryManager::updateEntry(identifier, url, getTitle(), m_webView->icon());
	}
}

void QtWebKitWebWidget::handleWindowCloseRequest()
{
	const QString mode = SettingsManager::getValue(QLatin1String("Browser/JavaScriptCanCloseWindows"), getUrl()).toString();

	if (mode != QLatin1String("ask"))
	{
		if (mode == QLatin1String("allow"))
		{
			emit requestedCloseWindow();
		}

		return;
	}

	ContentsDialog dialog(Utils::getIcon(QLatin1String("dialog-warning")), tr("JavaScript"), tr("Webpage wants to close this tab, do you want to allow to close it?"), QString(), (QDialogButtonBox::Ok | QDialogButtonBox::Cancel), NULL, this);
	dialog.setCheckBox(tr("Do not show this message again"), false);

	connect(this, SIGNAL(aboutToReload()), &dialog, SLOT(close()));

	if (dialog.getCheckBoxState())
	{
		SettingsManager::setValue(QLatin1String("Browser/JavaScriptCanCloseWindows"), (dialog.isAccepted() ? QLatin1String("allow") : QLatin1String("disallow")));
	}

	if (dialog.isAccepted())
	{
		emit requestedCloseWindow();
	}
}

void QtWebKitWebWidget::handlePermissionRequest(QWebFrame *frame, QWebPage::Feature feature)
{
	notifyPermissionRequested(frame, feature, false);
}

void QtWebKitWebWidget::handlePermissionCancel(QWebFrame *frame, QWebPage::Feature feature)
{
	notifyPermissionRequested(frame, feature, true);
}

void QtWebKitWebWidget::openFormRequest(const QUrl &url, QNetworkAccessManager::Operation operation, QIODevice *outgoingData)
{
	m_webView->stop();

	QtWebKitWebWidget *widget = new QtWebKitWebWidget(isPrivate(), getBackend(), m_networkManager->clone());
	widget->setOptions(getOptions());
	widget->setZoom(getZoom());
	widget->openRequest(url, operation, outgoingData);

	emit requestedNewWindow(widget, NewTabOpen);
}

void QtWebKitWebWidget::pasteText(const QString &text)
{
	QMimeData *mimeData = new QMimeData();
	const QStringList mimeTypes = QGuiApplication::clipboard()->mimeData()->formats();

	for (int i = 0; i < mimeTypes.count(); ++i)
	{
		mimeData->setData(mimeTypes.at(i), QGuiApplication::clipboard()->mimeData()->data(mimeTypes.at(i)));
	}

	QGuiApplication::clipboard()->setText(text);

	triggerAction(ActionsManager::PasteAction);

	QGuiApplication::clipboard()->setMimeData(mimeData);
}

void QtWebKitWebWidget::notifyTitleChanged()
{
	emit titleChanged(getTitle());

	handleHistory();
}

void QtWebKitWebWidget::notifyUrlChanged(const QUrl &url)
{
	updateOptions(url);
	updatePageActions(url);
	updateNavigationActions();

	emit urlChanged(url);

	SessionsManager::markSessionModified();
}

void QtWebKitWebWidget::notifyIconChanged()
{
	emit iconChanged(getIcon());
}

void QtWebKitWebWidget::notifyPermissionRequested(QWebFrame *frame, QWebPage::Feature feature, bool cancel)
{
	QString option;

	if (feature == QWebPage::Geolocation)
	{
		option = QLatin1String("Browser/EnableGeolocation");
	}
	else if (feature == QWebPage::Notifications)
	{
		option = QLatin1String("Browser/EnableNotifications");
	}

	if (!option.isEmpty())
	{
		if (cancel)
		{
			emit requestedPermission(option, frame->url(), true);
		}
		else
		{
			const QString value = SettingsManager::getValue(option, frame->url()).toString();

			if (value == QLatin1String("allow"))
			{
				m_page->setFeaturePermission(frame, feature, QWebPage::PermissionGrantedByUser);
			}
			else if (value == QLatin1String("disallow"))
			{
				m_page->setFeaturePermission(frame, feature, QWebPage::PermissionDeniedByUser);
			}
			else
			{
				emit requestedPermission(option, frame->url(), false);
			}
		}
	}
}

void QtWebKitWebWidget::updateUndoText(const QString &text)
{
	getAction(ActionsManager::UndoAction)->setText(text.isEmpty() ? tr("Undo") : tr("Undo: %1").arg(text));
}

void QtWebKitWebWidget::updateRedoText(const QString &text)
{
	getAction(ActionsManager::RedoAction)->setText(text.isEmpty() ? tr("Redo") : tr("Redo: %1").arg(text));
}

void QtWebKitWebWidget::updatePageActions(const QUrl &url)
{
	if (m_actions.contains(ActionsManager::AddBookmarkAction))
	{
		m_actions[ActionsManager::AddBookmarkAction]->setOverrideText(BookmarksManager::hasBookmark(url) ? QT_TRANSLATE_NOOP("actions", "Edit Bookmark…") : QT_TRANSLATE_NOOP("actions", "Add Bookmark…"));
	}

	if (m_actions.contains(ActionsManager::WebsitePreferencesAction))
	{
		m_actions[ActionsManager::WebsitePreferencesAction]->setEnabled(!url.isEmpty() && url.scheme() != QLatin1String("about"));
	}
}

void QtWebKitWebWidget::updateNavigationActions()
{
	if (m_actions.contains(ActionsManager::GoBackAction))
	{
		m_actions[ActionsManager::GoBackAction]->setEnabled(m_webView->history()->canGoBack());
	}

	if (m_actions.contains(ActionsManager::GoForwardAction))
	{
		m_actions[ActionsManager::GoForwardAction]->setEnabled(m_webView->history()->canGoForward());
	}

	if (m_actions.contains(ActionsManager::RewindAction))
	{
		m_actions[ActionsManager::RewindAction]->setEnabled(m_webView->history()->canGoBack());
	}

	if (m_actions.contains(ActionsManager::FastForwardAction))
	{
		m_actions[ActionsManager::FastForwardAction]->setEnabled(m_webView->history()->canGoForward());
	}

	if (m_actions.contains(ActionsManager::StopAction))
	{
		m_actions[ActionsManager::StopAction]->setEnabled(m_isLoading);
	}

	if (m_actions.contains(ActionsManager::ReloadAction))
	{
		m_actions[ActionsManager::ReloadAction]->setEnabled(!m_isLoading);
	}

	if (m_actions.contains(ActionsManager::ReloadOrStopAction))
	{
		m_actions[ActionsManager::ReloadOrStopAction]->setup(ActionsManager::getAction((m_isLoading ? ActionsManager::StopAction : ActionsManager::ReloadAction), this));
	}

	if (m_actions.contains(ActionsManager::LoadPluginsAction))
	{
		m_actions[ActionsManager::LoadPluginsAction]->setEnabled(findChildren<QtWebKitPluginWidget*>().count() > 0);
	}
}

void QtWebKitWebWidget::updateEditActions()
{
	if (m_actions.contains(ActionsManager::CutAction))
	{
		m_actions[ActionsManager::CutAction]->setEnabled(m_page->action(QWebPage::Cut)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::CopyAction))
	{
		m_actions[ActionsManager::CopyAction]->setEnabled(m_page->action(QWebPage::Copy)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::CopyPlainTextAction))
	{
		m_actions[ActionsManager::CopyPlainTextAction]->setEnabled(m_page->action(QWebPage::Copy)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::CopyToNoteAction))
	{
		m_actions[ActionsManager::CopyToNoteAction]->setEnabled(m_page->action(QWebPage::Copy)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::PasteAction))
	{
		m_actions[ActionsManager::PasteAction]->setEnabled(m_page->action(QWebPage::Paste)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::PasteAndGoAction))
	{
		m_actions[ActionsManager::PasteAndGoAction]->setEnabled(!QApplication::clipboard()->text().isEmpty());
	}

	if (m_actions.contains(ActionsManager::PasteNoteAction))
	{
		m_actions[ActionsManager::PasteNoteAction]->setEnabled(m_page->action(QWebPage::Paste)->isEnabled() && NotesManager::getModel()->getRootItem()->hasChildren());
	}

	if (m_actions.contains(ActionsManager::DeleteAction))
	{
		m_actions[ActionsManager::DeleteAction]->setEnabled(m_page->action(QWebPage::DeleteEndOfWord)->isEnabled());
	}

	if (m_actions.contains(ActionsManager::ClearAllAction))
	{
		const QWebElement element = m_hitResult.element();
		const QString tagName = element.tagName().toLower();
		const QString type = element.attribute(QLatin1String("type")).toLower();

		m_actions[ActionsManager::ClearAllAction]->setEnabled(m_hitResult.isContentEditable() && ((tagName == QLatin1String("textarea") && !element.toPlainText().isEmpty()) || (tagName == QLatin1String("input") && (type.isEmpty() || type == QLatin1String("text") || type == QLatin1String("search")) && !element.attribute(QLatin1String("value")).isEmpty())));
	}

	if (m_actions.contains(ActionsManager::SearchAction))
	{
		const SearchInformation engine = SearchesManager::getSearchEngine(getOption(QLatin1String("Search/DefaultQuickSearchEngine")).toString());
		const bool isValid = !engine.identifier.isEmpty();

		m_actions[ActionsManager::SearchAction]->setEnabled(isValid);
		m_actions[ActionsManager::SearchAction]->setData(isValid ? engine.identifier : QVariant());
		m_actions[ActionsManager::SearchAction]->setIcon((!isValid || engine.icon.isNull()) ? Utils::getIcon(QLatin1String("edit-find")) : engine.icon);
		m_actions[ActionsManager::SearchAction]->setOverrideText(isValid ? engine.title : QT_TRANSLATE_NOOP("actions", "Search"));
		m_actions[ActionsManager::SearchAction]->setToolTip(isValid ? engine.description : tr("No search engines defined"));
	}

	if (m_actions.contains(ActionsManager::SearchMenuAction))
	{
		m_actions[ActionsManager::SearchMenuAction]->setEnabled(SearchesManager::getSearchEngines().count() > 1);
	}

	updateLinkActions();
	updateFrameActions();
	updateImageActions();
	updateMediaActions();
}

void QtWebKitWebWidget::updateLinkActions()
{
	const bool isLink = m_hitResult.linkUrl().isValid();

	if (m_actions.contains(ActionsManager::OpenLinkAction))
	{
		m_actions[ActionsManager::OpenLinkAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInCurrentTabAction))
	{
		m_actions[ActionsManager::OpenLinkInCurrentTabAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewTabAction))
	{
		m_actions[ActionsManager::OpenLinkInNewTabAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewTabBackgroundAction))
	{
		m_actions[ActionsManager::OpenLinkInNewTabBackgroundAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewWindowAction))
	{
		m_actions[ActionsManager::OpenLinkInNewWindowAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewWindowBackgroundAction))
	{
		m_actions[ActionsManager::OpenLinkInNewWindowBackgroundAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewPrivateTabAction))
	{
		m_actions[ActionsManager::OpenLinkInNewPrivateTabAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewPrivateTabBackgroundAction))
	{
		m_actions[ActionsManager::OpenLinkInNewPrivateTabBackgroundAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewPrivateWindowAction))
	{
		m_actions[ActionsManager::OpenLinkInNewPrivateWindowAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::OpenLinkInNewPrivateWindowBackgroundAction))
	{
		m_actions[ActionsManager::OpenLinkInNewPrivateWindowBackgroundAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::CopyLinkToClipboardAction))
	{
		m_actions[ActionsManager::CopyLinkToClipboardAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::BookmarkLinkAction))
	{
		m_actions[ActionsManager::BookmarkLinkAction]->setOverrideText(BookmarksManager::hasBookmark(m_hitResult.linkUrl()) ? QT_TRANSLATE_NOOP("actions", "Edit Link Bookmark…") : QT_TRANSLATE_NOOP("actions", "Bookmark Link…"));
		m_actions[ActionsManager::BookmarkLinkAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::SaveLinkToDiskAction))
	{
		m_actions[ActionsManager::SaveLinkToDiskAction]->setEnabled(isLink);
	}

	if (m_actions.contains(ActionsManager::SaveLinkToDownloadsAction))
	{
		m_actions[ActionsManager::SaveLinkToDownloadsAction]->setEnabled(isLink);
	}
}

void QtWebKitWebWidget::updateFrameActions()
{
	const bool isFrame = (m_hitResult.frame() && m_hitResult.frame() != m_page->mainFrame());

	if (m_actions.contains(ActionsManager::OpenFrameInCurrentTabAction))
	{
		m_actions[ActionsManager::OpenFrameInCurrentTabAction]->setEnabled(isFrame);
	}

	if (m_actions.contains(ActionsManager::OpenFrameInNewTabAction))
	{
		m_actions[ActionsManager::OpenFrameInNewTabAction]->setEnabled(isFrame);
	}

	if (m_actions.contains(ActionsManager::OpenFrameInNewTabBackgroundAction))
	{
		m_actions[ActionsManager::OpenFrameInNewTabBackgroundAction]->setEnabled(isFrame);
	}

	if (m_actions.contains(ActionsManager::CopyFrameLinkToClipboardAction))
	{
		m_actions[ActionsManager::CopyFrameLinkToClipboardAction]->setEnabled(isFrame);
	}

	if (m_actions.contains(ActionsManager::ReloadFrameAction))
	{
		m_actions[ActionsManager::ReloadFrameAction]->setEnabled(isFrame);
	}

	if (m_actions.contains(ActionsManager::ViewFrameSourceAction))
	{
		m_actions[ActionsManager::ViewFrameSourceAction]->setEnabled(false);
	}
}

void QtWebKitWebWidget::updateImageActions()
{
	const bool isImage = (!m_hitResult.pixmap().isNull() || !m_hitResult.imageUrl().isEmpty() || m_hitResult.element().tagName().toLower() == QLatin1String("img"));
	const bool isOpened = getUrl().matches(m_hitResult.imageUrl(), (QUrl::NormalizePathSegments | QUrl::RemoveFragment | QUrl::StripTrailingSlash));
	const QString fileName = fontMetrics().elidedText(m_hitResult.imageUrl().fileName(), Qt::ElideMiddle, 256);

	if (m_actions.contains(ActionsManager::OpenImageInNewTabAction))
	{
		m_actions[ActionsManager::OpenImageInNewTabAction]->setOverrideText(isImage ? (fileName.isEmpty() || m_hitResult.imageUrl().scheme() == QLatin1String("data")) ? tr("Open Image (Untitled)") : tr("Open Image (%1)").arg(fileName) : QT_TRANSLATE_NOOP("actions", "Open Image"));
		m_actions[ActionsManager::OpenImageInNewTabAction]->setEnabled(isImage && !isOpened);
	}

	if (m_actions.contains(ActionsManager::SaveImageToDiskAction))
	{
		m_actions[ActionsManager::SaveImageToDiskAction]->setEnabled(isImage);
	}

	if (m_actions.contains(ActionsManager::CopyImageToClipboardAction))
	{
		m_actions[ActionsManager::CopyImageToClipboardAction]->setEnabled(isImage);
	}

	if (m_actions.contains(ActionsManager::CopyImageUrlToClipboardAction))
	{
		m_actions[ActionsManager::CopyImageUrlToClipboardAction]->setEnabled(isImage);
	}

	if (m_actions.contains(ActionsManager::ReloadImageAction))
	{
		m_actions[ActionsManager::ReloadImageAction]->setEnabled(isImage);
	}

	if (m_actions.contains(ActionsManager::ImagePropertiesAction))
	{
		m_actions[ActionsManager::ImagePropertiesAction]->setEnabled(isImage);
	}
}

void QtWebKitWebWidget::updateMediaActions()
{
#if QTWEBKIT_VERSION >= 0x050200
	const bool isMedia = m_hitResult.mediaUrl().isValid();
#else
	const bool isMedia = false;
#endif
	const bool isVideo = (m_hitResult.element().tagName().toLower() == QLatin1String("video"));
	const bool isPaused = m_hitResult.element().evaluateJavaScript(QLatin1String("this.paused")).toBool();
	const bool isMuted = m_hitResult.element().evaluateJavaScript(QLatin1String("this.muted")).toBool();

	if (m_actions.contains(ActionsManager::SaveMediaToDiskAction))
	{
		m_actions[ActionsManager::SaveMediaToDiskAction]->setOverrideText(isVideo ? QT_TRANSLATE_NOOP("actions", "Save Video…") : QT_TRANSLATE_NOOP("actions", "Save Audio…"));
		m_actions[ActionsManager::SaveMediaToDiskAction]->setEnabled(isMedia);
	}

	if (m_actions.contains(ActionsManager::CopyMediaUrlToClipboardAction))
	{
		m_actions[ActionsManager::CopyMediaUrlToClipboardAction]->setOverrideText(isVideo ? QT_TRANSLATE_NOOP("actions", "Copy Video Link to Clipboard") : QT_TRANSLATE_NOOP("actions", "Copy Audio Link to Clipboard"));
		m_actions[ActionsManager::CopyMediaUrlToClipboardAction]->setEnabled(isMedia);
	}

	if (m_actions.contains(ActionsManager::MediaControlsAction))
	{
		m_actions[ActionsManager::MediaControlsAction]->setChecked(m_hitResult.element().evaluateJavaScript(QLatin1String("this.loop")).toBool());
		m_actions[ActionsManager::MediaControlsAction]->setEnabled(isMedia);
	}

	if (m_actions.contains(ActionsManager::MediaLoopAction))
	{
		m_actions[ActionsManager::MediaLoopAction]->setChecked(m_hitResult.element().evaluateJavaScript(QLatin1String("this.looped")).toBool());
		m_actions[ActionsManager::MediaLoopAction]->setEnabled(isMedia);
	}

	if (m_actions.contains(ActionsManager::MediaPlayPauseAction))
	{
		m_actions[ActionsManager::MediaPlayPauseAction]->setOverrideText(isPaused ? QT_TRANSLATE_NOOP("actions", "Play") : QT_TRANSLATE_NOOP("actions", "Pause"));
		m_actions[ActionsManager::MediaPlayPauseAction]->setIcon(Utils::getIcon(isPaused ? QLatin1String("media-playback-start") : QLatin1String("media-playback-pause")));
		m_actions[ActionsManager::MediaPlayPauseAction]->setEnabled(isMedia);
	}

	if (m_actions.contains(ActionsManager::MediaMuteAction))
	{
		m_actions[ActionsManager::MediaMuteAction]->setOverrideText(isMuted ? QT_TRANSLATE_NOOP("actions", "Unmute") : QT_TRANSLATE_NOOP("actions", "Mute"));
		m_actions[ActionsManager::MediaMuteAction]->setIcon(Utils::getIcon(isMuted ? QLatin1String("audio-volume-medium") : QLatin1String("audio-volume-muted")));
		m_actions[ActionsManager::MediaMuteAction]->setEnabled(isMedia);
	}
}

void QtWebKitWebWidget::updateBookmarkActions()
{
	updatePageActions(getUrl());
	updateLinkActions();
}

void QtWebKitWebWidget::updateOptions(const QUrl &url)
{
	QWebSettings *settings = m_webView->page()->settings();
	settings->setAttribute(QWebSettings::AutoLoadImages, getOption(QLatin1String("Browser/EnableImages"), url).toBool());
	settings->setAttribute(QWebSettings::PluginsEnabled, getOption(QLatin1String("Browser/EnablePlugins"), url).toString() != QLatin1String("disabled"));
	settings->setAttribute(QWebSettings::JavaEnabled, getOption(QLatin1String("Browser/EnableJava"), url).toBool());
	settings->setAttribute(QWebSettings::JavascriptEnabled, getOption(QLatin1String("Browser/EnableJavaScript"), url).toBool());
	settings->setAttribute(QWebSettings::JavascriptCanAccessClipboard, getOption(QLatin1String("Browser/JavaScriptCanAccessClipboard"), url).toBool());
	settings->setAttribute(QWebSettings::JavascriptCanCloseWindows, getOption(QLatin1String("Browser/JavaScriptCanCloseWindows"), url).toBool());
	settings->setAttribute(QWebSettings::JavascriptCanOpenWindows, getOption(QLatin1String("Browser/JavaScriptCanOpenWindows"), url).toBool());
	settings->setAttribute(QWebSettings::LocalStorageEnabled, getOption(QLatin1String("Browser/EnableLocalStorage"), url).toBool());
	settings->setAttribute(QWebSettings::OfflineStorageDatabaseEnabled, getOption(QLatin1String("Browser/EnableOfflineStorageDatabase"), url).toBool());
	settings->setAttribute(QWebSettings::OfflineWebApplicationCacheEnabled, getOption(QLatin1String("Browser/EnableOfflineWebApplicationCache"), url).toBool());
	settings->setDefaultTextEncoding(getOption(QLatin1String("Content/DefaultCharacterEncoding"), url).toString());

	disconnect(m_webView->page(), SIGNAL(statusBarMessage(QString)), this, SLOT(setStatusMessage(QString)));

	if (getOption(QLatin1String("Browser/JavaScriptCanShowStatusMessages"), url).toBool())
	{
		connect(m_webView->page(), SIGNAL(statusBarMessage(QString)), this, SLOT(setStatusMessage(QString)));
	}
	else
	{
		setStatusMessage(QString());
	}

	m_contentBlockingProfiles = ContentBlockingManager::getProfileList(getOption(QLatin1String("Content/BlockingProfiles"), url).toStringList());

	m_page->updateStyleSheets(url);

	m_networkManager->updateOptions(url);

	m_canLoadPlugins = (getOption(QLatin1String("Browser/EnablePlugins"), url).toString() == QLatin1String("enabled"));
}

void QtWebKitWebWidget::clearOptions()
{
	WebWidget::clearOptions();

	updateOptions(getUrl());
}

void QtWebKitWebWidget::clearSelection()
{
	const QWebElement element = m_page->mainFrame()->findFirstElement(QLatin1String(":focus"));

	if (element.tagName().toLower() == QLatin1String("textarea") || element.tagName().toLower() == QLatin1String("input"))
	{
		m_page->triggerAction(QWebPage::MoveToPreviousChar);
	}
	else
	{
		m_page->mainFrame()->evaluateJavaScript(QLatin1String("window.getSelection().empty()"));
	}
}


void QtWebKitWebWidget::goToHistoryIndex(int index)
{
	m_webView->history()->goToItem(m_webView->history()->itemAt(index));
}

void QtWebKitWebWidget::triggerAction(int identifier, bool checked)
{
	switch (identifier)
	{
		case ActionsManager::OpenLinkAction:
			{
				QMouseEvent mousePressEvent(QEvent::MouseButtonPress, QPointF(m_clickPosition), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
				QMouseEvent mouseReleaseEvent(QEvent::MouseButtonRelease, QPointF(m_clickPosition), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);

				QCoreApplication::sendEvent(m_webView, &mousePressEvent);
				QCoreApplication::sendEvent(m_webView, &mouseReleaseEvent);

				m_clickPosition = QPoint();
			}

			break;
		case ActionsManager::OpenLinkInCurrentTabAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), CurrentTabOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewTabAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewTabOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewTabBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewBackgroundTabOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewWindowAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewWindowOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewWindowBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewBackgroundWindowOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewPrivateTabAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewPrivateTabOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewPrivateTabBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewPrivateBackgroundTabOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewPrivateWindowAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewPrivateWindowOpen);
			}

			break;
		case ActionsManager::OpenLinkInNewPrivateWindowBackgroundAction:
			if (m_hitResult.linkUrl().isValid())
			{
				openUrl(m_hitResult.linkUrl(), NewPrivateBackgroundWindowOpen);
			}

			break;
		case ActionsManager::CopyLinkToClipboardAction:
			if (!m_hitResult.linkUrl().isEmpty())
			{
				QGuiApplication::clipboard()->setText(m_hitResult.linkUrl().toString());
			}

			break;
		case ActionsManager::BookmarkLinkAction:
			if (m_hitResult.linkUrl().isValid())
			{
				const QString title = m_hitResult.element().attribute(QLatin1String("title"));

				emit requestedAddBookmark(m_hitResult.linkUrl(), (title.isEmpty() ? m_hitResult.element().toPlainText() : title), QString());
			}

			break;
		case ActionsManager::SaveLinkToDiskAction:
			m_webView->page()->triggerAction(QWebPage::DownloadLinkToDisk);

			break;
		case ActionsManager::SaveLinkToDownloadsAction:
			TransfersManager::startTransfer(m_hitResult.linkUrl().toString(), QString(), true, isPrivate());

			break;
		case ActionsManager::OpenSelectionAsLinkAction:
			{
				const QString text(m_webView->selectedText());

				if (!text.isEmpty())
				{
					InputInterpreter *interpreter = new InputInterpreter(this);

					connect(interpreter, SIGNAL(requestedOpenUrl(QUrl,OpenHints)), this, SIGNAL(requestedOpenUrl(QUrl,OpenHints)));
					connect(interpreter, SIGNAL(requestedSearch(QString,QString,OpenHints)), this, SIGNAL(requestedSearch(QString,QString,OpenHints)));

					interpreter->interpret(text, WindowsManager::calculateOpenHints(QGuiApplication::keyboardModifiers()), true);
				}
			}

			break;
		case ActionsManager::OpenFrameInCurrentTabAction:
			if (m_hitResult.frame())
			{
				setUrl(m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());
			}

			break;
		case ActionsManager::OpenFrameInNewTabAction:
			if (m_hitResult.frame())
			{
				openUrl((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()), CurrentTabOpen);
			}

			break;
		case ActionsManager::OpenFrameInNewTabBackgroundAction:
			if (m_hitResult.frame())
			{
				openUrl((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()), NewBackgroundTabOpen);
			}

			break;
		case ActionsManager::CopyFrameLinkToClipboardAction:
			if (m_hitResult.frame())
			{
				QGuiApplication::clipboard()->setText((m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl()).toString());
			}

			break;
		case ActionsManager::ReloadFrameAction:
			if (m_hitResult.frame())
			{
				const QUrl url = (m_hitResult.frame()->url().isValid() ? m_hitResult.frame()->url() : m_hitResult.frame()->requestedUrl());

				m_hitResult.frame()->setUrl(QUrl());
				m_hitResult.frame()->setUrl(url);
			}

			break;
		case ActionsManager::OpenImageInNewTabAction:
			if (!m_hitResult.imageUrl().isEmpty())
			{
				openUrl(m_hitResult.imageUrl(), NewTabOpen);
			}

			break;
		case ActionsManager::SaveImageToDiskAction:
			if (m_hitResult.imageUrl().isValid())
			{
				downloadFile(QNetworkRequest(m_hitResult.imageUrl()));
			}

			break;
		case ActionsManager::CopyImageToClipboardAction:
			m_webView->page()->triggerAction(QWebPage::CopyImageToClipboard);

			break;
		case ActionsManager::CopyImageUrlToClipboardAction:
			if (!m_hitResult.imageUrl().isEmpty())
			{
				QApplication::clipboard()->setText(m_hitResult.imageUrl().toString());
			}

			break;
		case ActionsManager::ReloadImageAction:
			if ((!m_hitResult.imageUrl().isEmpty() || m_hitResult.element().tagName().toLower() == QLatin1String("img")) && !m_hitResult.element().isNull())
			{
				if (getUrl().matches(m_hitResult.imageUrl(), (QUrl::NormalizePathSegments | QUrl::RemoveFragment | QUrl::StripTrailingSlash)))
				{
					triggerAction(ActionsManager::ReloadAndBypassCacheAction);
				}
				else
				{
					const QUrl url(m_hitResult.imageUrl());
					const QString src = m_hitResult.element().attribute(QLatin1String("src"));
					NetworkCache *cache = NetworkManagerFactory::getCache();

					m_hitResult.element().setAttribute(QLatin1String("src"), QString());

					if (cache)
					{
						cache->remove(url);
					}

					m_hitResult.element().setAttribute(QLatin1String("src"), src);

					m_webView->page()->mainFrame()->evaluateJavaScript(QStringLiteral("var images = document.querySelectorAll('img[src=\"%1\"]'); for (var i = 0; i < images.length; ++i) { images[i].src = ''; images[i].src = \'%1\'; }").arg(src));
				}
			}

			break;
		case ActionsManager::ImagePropertiesAction:
			{
				QVariantMap properties;
				properties[QLatin1String("alternativeText")] = m_hitResult.element().attribute(QLatin1String("alt"));
				properties[QLatin1String("longDescription")] = m_hitResult.element().attribute(QLatin1String("longdesc"));

				if (!m_hitResult.pixmap().isNull())
				{
					properties[QLatin1String("width")] = m_hitResult.pixmap().width();
					properties[QLatin1String("height")] = m_hitResult.pixmap().height();
					properties[QLatin1String("depth")] = m_hitResult.pixmap().depth();
				}

				ContentsWidget *parent = qobject_cast<ContentsWidget*>(parentWidget());
				ImagePropertiesDialog *imagePropertiesDialog = new ImagePropertiesDialog(m_hitResult.imageUrl(), properties, (m_networkManager->cache() ? m_networkManager->cache()->data(m_hitResult.imageUrl()) : NULL), this);
				imagePropertiesDialog->setButtonsVisible(false);

				if (parent)
				{
					ContentsDialog dialog(Utils::getIcon(QLatin1String("dialog-information")), imagePropertiesDialog->windowTitle(), QString(), QString(), (QDialogButtonBox::Close), imagePropertiesDialog, this);

					connect(this, SIGNAL(aboutToReload()), &dialog, SLOT(close()));

					showDialog(&dialog);
				}
			}

			break;
#if QTWEBKIT_VERSION >= 0x050200
		case ActionsManager::SaveMediaToDiskAction:
			if (m_hitResult.mediaUrl().isValid())
			{
				downloadFile(QNetworkRequest(m_hitResult.mediaUrl()));
			}

			break;
		case ActionsManager::CopyMediaUrlToClipboardAction:
			if (!m_hitResult.mediaUrl().isEmpty())
			{
				QApplication::clipboard()->setText(m_hitResult.mediaUrl().toString());
			}

			break;
		case ActionsManager::MediaControlsAction:
			m_webView->page()->triggerAction(QWebPage::ToggleMediaControls, checked);

			break;
		case ActionsManager::MediaLoopAction:
			m_webView->page()->triggerAction(QWebPage::ToggleMediaLoop, checked);

			break;
		case ActionsManager::MediaPlayPauseAction:
			m_webView->page()->triggerAction(QWebPage::ToggleMediaPlayPause);

			break;
		case ActionsManager::MediaMuteAction:
			m_webView->page()->triggerAction(QWebPage::ToggleMediaMute);

			break;
#endif
		case ActionsManager::GoBackAction:
			m_webView->page()->triggerAction(QWebPage::Back);

			break;
		case ActionsManager::GoForwardAction:
			m_webView->page()->triggerAction(QWebPage::Forward);

			break;
		case ActionsManager::RewindAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(0));

			break;
		case ActionsManager::FastForwardAction:
			m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(m_webView->page()->history()->count() - 1));

			break;
		case ActionsManager::StopAction:
			m_webView->page()->triggerAction(QWebPage::Stop);

			break;
		case ActionsManager::StopScheduledReloadAction:
			m_webView->page()->triggerAction(QWebPage::StopScheduledPageRefresh);

			break;
		case ActionsManager::ReloadAction:
			emit aboutToReload();

			m_webView->page()->triggerAction(QWebPage::Stop);
			m_webView->page()->triggerAction(QWebPage::Reload);

			break;
		case ActionsManager::ReloadOrStopAction:
			if (isLoading())
			{
				triggerAction(ActionsManager::StopAction);
			}
			else
			{
				triggerAction(ActionsManager::ReloadAction);
			}

			break;
		case ActionsManager::ReloadAndBypassCacheAction:
			m_webView->page()->triggerAction(QWebPage::ReloadAndBypassCache);

			break;
		case ActionsManager::ContextMenuAction:
			{
				const QWebElement element = m_page->mainFrame()->findFirstElement(QLatin1String(":focus"));

				if (element.isNull())
				{
					m_clickPosition = m_webView->mapFromGlobal(QCursor::pos());
				}
				else
				{
					m_clickPosition = element.geometry().center();

					QWebFrame *frame = element.webFrame();

					while (frame)
					{
						m_clickPosition -= frame->scrollPosition();

						frame = frame->parentFrame();
					}
				}

				showContextMenu(m_clickPosition);
			}

			break;
		case ActionsManager::UndoAction:
			m_webView->page()->triggerAction(QWebPage::Undo);

			break;
		case ActionsManager::RedoAction:
			m_webView->page()->triggerAction(QWebPage::Redo);

			break;
		case ActionsManager::CutAction:
			m_webView->page()->triggerAction(QWebPage::Cut);

			break;
		case ActionsManager::CopyAction:
			m_webView->page()->triggerAction(QWebPage::Copy);

			break;
		case ActionsManager::CopyPlainTextAction:
			{
				const QString text = getSelectedText();

				if (!text.isEmpty())
				{
					QApplication::clipboard()->setText(text);
				}
			}

			break;
		case ActionsManager::CopyAddressAction:
			QApplication::clipboard()->setText(getUrl().toString());

			break;
		case ActionsManager::CopyToNoteAction:
			{
				BookmarksItem *note = NotesManager::addNote(BookmarksModel::UrlBookmark, getUrl());
				note->setData(getSelectedText(), BookmarksModel::DescriptionRole);
			}

			break;
		case ActionsManager::PasteAction:
			m_webView->page()->triggerAction(QWebPage::Paste);

			break;
		case ActionsManager::DeleteAction:
			m_webView->page()->triggerAction(QWebPage::DeleteEndOfWord);

			break;
		case ActionsManager::SelectAllAction:
			m_webView->page()->triggerAction(QWebPage::SelectAll);

			break;
		case ActionsManager::ClearAllAction:
			triggerAction(ActionsManager::SelectAllAction);
			triggerAction(ActionsManager::DeleteAction);

			break;
		case ActionsManager::SearchAction:
			quickSearch(getAction(ActionsManager::SearchAction));

			break;
		case ActionsManager::CreateSearchAction:
			{
				QWebElement parentElement = m_hitResult.element().parent();

				while (!parentElement.isNull() && parentElement.tagName().toLower() != QLatin1String("form"))
				{
					parentElement = parentElement.parent();
				}

				const QWebElementCollection inputs = parentElement.findAll(QLatin1String("input:not([disabled])[name], select:not([disabled])[name], textarea:not([disabled])[name]"));

				if (!parentElement.isNull() && parentElement.hasAttribute(QLatin1String("action")) && inputs.count() > 0)
				{
					QUrlQuery parameters;

					for (int i = 0; i < inputs.count(); ++i)
					{
						QString value;

						if (inputs.at(i).tagName().toLower() == QLatin1String("textarea"))
						{
							value = inputs.at(i).toPlainText();
						}
						else if (inputs.at(i).tagName().toLower() == QLatin1String("select"))
						{
							const QWebElementCollection options = inputs.at(i).findAll(QLatin1String("option"));

							for (int j = 0; j < options.count(); ++j)
							{
								if (options.at(j).hasAttribute(QLatin1String("selected")))
								{
									value = options.at(j).attribute(QLatin1String("value"), options.at(j).toPlainText());

									break;
								}
							}
						}
						else
						{
							if ((inputs.at(i).attribute(QLatin1String("type")) == QLatin1String("checkbox") || inputs.at(i).attribute(QLatin1String("type")) == QLatin1String("radio")) && !inputs.at(i).hasAttribute(QLatin1String("checked")))
							{
								continue;
							}

							value = inputs.at(i).attribute(QLatin1String("value"));
						}

						parameters.addQueryItem(inputs.at(i).attribute(QLatin1String("name")), ((inputs.at(i) == m_hitResult.element()) ? QLatin1String("{searchTerms}") : value));
					}

					const QStringList identifiers = SearchesManager::getSearchEngines();
					const QStringList keywords = SearchesManager::getSearchKeywords();
					const QIcon icon = m_webView->icon();
					const QUrl url(parentElement.attribute(QLatin1String("action")));
					SearchInformation engine;
					engine.identifier = Utils::createIdentifier(getUrl().host(), identifiers);
					engine.title = getTitle();
					engine.icon = (icon.isNull() ? Utils::getIcon(QLatin1String("edit-find")) : icon);
					engine.resultsUrl.url = (url.isEmpty() ? getUrl() : (url.isRelative() ? getUrl().resolved(url) : url)).toString();
					engine.resultsUrl.enctype = parentElement.attribute(QLatin1String("enctype"));
					engine.resultsUrl.method = ((parentElement.attribute(QLatin1String("method"), QLatin1String("get")).toLower() == QLatin1String("post")) ? QLatin1String("post") : QLatin1String("get"));
					engine.resultsUrl.parameters = parameters;

					SearchPropertiesDialog dialog(engine, keywords, false, this);

					if (dialog.exec() == QDialog::Rejected)
					{
						return;
					}

					SearchesManager::addSearchEngine(dialog.getSearchEngine(), dialog.isDefault());
				}
			}

			break;
		case ActionsManager::ScrollToStartAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(m_webView->page()->mainFrame()->scrollPosition().x(), 0));

			break;
		case ActionsManager::ScrollToEndAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(m_webView->page()->mainFrame()->scrollPosition().x(), m_webView->page()->mainFrame()->scrollBarMaximum(Qt::Vertical)));

			break;
		case ActionsManager::ScrollPageUpAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(m_webView->page()->mainFrame()->scrollPosition().x(), qMax(0, (m_webView->page()->mainFrame()->scrollPosition().y() - m_webView->height()))));

			break;
		case ActionsManager::ScrollPageDownAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(m_webView->page()->mainFrame()->scrollPosition().x(), qMin(m_webView->page()->mainFrame()->scrollBarMaximum(Qt::Vertical), (m_webView->page()->mainFrame()->scrollPosition().y() + m_webView->height()))));

			break;
		case ActionsManager::ScrollPageLeftAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(qMax(0, (m_webView->page()->mainFrame()->scrollPosition().x() - m_webView->width())), m_webView->page()->mainFrame()->scrollPosition().y()));

			break;
		case ActionsManager::ScrollPageRightAction:
			m_webView->page()->mainFrame()->setScrollPosition(QPoint(qMin(m_webView->page()->mainFrame()->scrollBarMaximum(Qt::Horizontal), (m_webView->page()->mainFrame()->scrollPosition().x() + m_webView->width())), m_webView->page()->mainFrame()->scrollPosition().y()));

			break;
		case ActionsManager::StartDragScrollAction:
			setScrollMode(DragScroll);

			break;
		case ActionsManager::StartMoveScrollAction:
			setScrollMode(MoveScroll);

			break;
		case ActionsManager::EndScrollAction:
			setScrollMode(NoScroll);

			break;
		case ActionsManager::ActivateContentAction:
			{
				m_webView->setFocus();

				m_page->mainFrame()->setFocus();

				QWebElement element = m_page->mainFrame()->findFirstElement(QLatin1String(":focus"));

				if (element.tagName().toLower() == QLatin1String("textarea") || element.tagName().toLower() == QLatin1String("input"))
				{
					m_page->mainFrame()->evaluateJavaScript(QLatin1String("document.activeElement.blur()"));
				}
			}

			break;
		case ActionsManager::AddBookmarkAction:
			{
				const QString description = m_page->mainFrame()->findFirstElement(QLatin1String("[name=\"description\"]")).attribute(QLatin1String("content"));

				emit requestedAddBookmark(getUrl(), getTitle(), (description.isEmpty() ? m_page->mainFrame()->findFirstElement(QLatin1String("[name=\"og:description\"]")).attribute(QLatin1String("property")) : description));
			}

			break;
		case ActionsManager::LoadPluginsAction:
			{
				m_canLoadPlugins = true;

				QList<QWebFrame*> frames;
				frames.append(m_page->mainFrame());

				while (!frames.isEmpty())
				{
					QWebFrame *frame = frames.takeFirst();
					const QWebElementCollection elements = frame->documentElement().findAll(QLatin1String("object, embed"));

					for (int i = 0; i < elements.count(); ++i)
					{
						elements.at(i).replace(elements.at(i).clone());
					}

					frames.append(frame->childFrames());
				}

				if (m_actions.contains(ActionsManager::LoadPluginsAction))
				{
					getAction(ActionsManager::LoadPluginsAction)->setEnabled(false);
				}
			}

			break;
		case ActionsManager::InspectPageAction:
			if (!m_inspector)
			{
				m_inspector = new QWebInspector(this);
				m_inspector->setPage(m_webView->page());
				m_inspector->setContextMenuPolicy(Qt::NoContextMenu);
				m_inspector->setMinimumHeight(200);
				m_inspector->installEventFilter(this);

				m_splitter->addWidget(m_inspector);

				m_inspectorCloseButton = new QToolButton(m_inspector);
				m_inspectorCloseButton->setAutoFillBackground(false);
				m_inspectorCloseButton->setAutoRaise(true);
				m_inspectorCloseButton->setIcon(Utils::getIcon(QLatin1String("window-close")));
				m_inspectorCloseButton->setToolTip(tr("Close"));

				connect(m_inspectorCloseButton, SIGNAL(clicked()), this, SLOT(hideInspector()));
			}

			m_inspector->setVisible(checked);

			if (checked)
			{
				m_inspectorCloseButton->setFixedSize(16, 16);
				m_inspectorCloseButton->show();
				m_inspectorCloseButton->raise();
				m_inspectorCloseButton->move(QPoint((m_inspector->width() - 19), 3));
			}
			else
			{
				m_inspectorCloseButton->hide();
			}

			getAction(ActionsManager::InspectPageAction)->setChecked(checked);

			emit progressBarGeometryChanged();

			break;
		case ActionsManager::InspectElementAction:
			triggerAction(ActionsManager::InspectPageAction, true);

			m_webView->triggerPageAction(QWebPage::InspectElement);

			break;
		case ActionsManager::WebsitePreferencesAction:
			{
				const QUrl url(getUrl());
				WebsitePreferencesDialog dialog(url, m_networkManager->getCookieJar()->getCookies(url.host()), this);

				if (dialog.exec() == QDialog::Accepted)
				{
					updateOptions(getUrl());
				}
			}

			break;
		default:
			break;
	}
}

void QtWebKitWebWidget::showContextMenu(const QPoint &position)
{
	if (m_ignoreContextMenu || (position.isNull() && (m_webView->selectedText().isEmpty() || m_clickPosition.isNull())))
	{
		return;
	}

	const QPoint hitPosition = (position.isNull() ? m_clickPosition : position);

	if (isScrollBar(hitPosition))
	{
		return;
	}

	if (SettingsManager::getValue(QLatin1String("Browser/JavaScriptCanDisableContextMenu")).toBool())
	{
		QContextMenuEvent menuEvent(QContextMenuEvent::Other, hitPosition, m_webView->mapToGlobal(hitPosition), Qt::NoModifier);

		if (m_page->swallowContextMenuEvent(&menuEvent))
		{
			return;
		}
	}

	QWebFrame *frame = m_webView->page()->frameAt(hitPosition);

	if (!frame)
	{
		return;
	}

	m_hitResult = frame->hitTestContent(hitPosition);

	updateEditActions();

	MenuFlags flags = NoMenu;
	const QString tagName = m_hitResult.element().tagName().toLower();
	const QString tagType = m_hitResult.element().attribute(QLatin1String("type")).toLower();

	if (tagName == QLatin1String("textarea") || tagName == QLatin1String("select") || (tagName == QLatin1String("input") && (tagType.isEmpty() || tagType == QLatin1String("text") || tagType == QLatin1String("search"))))
	{
		QWebElement parentElement = m_hitResult.element().parent();

		while (!parentElement.isNull() && parentElement.tagName().toLower() != QLatin1String("form"))
		{
			parentElement = parentElement.parent();
		}

		if (!parentElement.isNull() && parentElement.hasAttribute(QLatin1String("action")) && !parentElement.findFirst(QLatin1String("input[name], select[name], textarea[name]")).isNull())
		{
			flags |= FormMenu;
		}
	}

	if (m_hitResult.pixmap().isNull() && m_hitResult.isContentSelected() && !m_webView->selectedText().isEmpty())
	{
		flags |= SelectionMenu;
	}

	if (m_hitResult.linkUrl().isValid())
	{
		if (m_hitResult.linkUrl().scheme() == QLatin1String("mailto"))
		{
			flags |= MailMenu;
		}
		else
		{
			flags |= LinkMenu;
		}
	}

	if (!m_hitResult.pixmap().isNull() || !m_hitResult.imageUrl().isEmpty() || m_hitResult.element().tagName().toLower() == QLatin1String("img"))
	{
		flags |= ImageMenu;
	}

#if QTWEBKIT_VERSION >= 0x050200
	if (m_hitResult.mediaUrl().isValid())
	{
		flags |= MediaMenu;
	}
#endif

	if (m_hitResult.isContentEditable())
	{
		flags |= EditMenu;
	}

	if (flags == NoMenu || flags == FormMenu)
	{
		flags |= StandardMenu;

		if (m_hitResult.frame() != m_webView->page()->mainFrame())
		{
			flags |= FrameMenu;
		}
	}

	WebWidget::showContextMenu(hitPosition, flags);
}

void QtWebKitWebWidget::setHistory(const WindowHistoryInformation &history)
{
	if (history.entries.count() == 0)
	{
		m_webView->page()->history()->clear();

		updateNavigationActions();
		updateOptions(QUrl());
		updatePageActions(QUrl());

		return;
	}

	const int index = qMin(history.index, (m_webView->history()->maximumItemCount() - 1));
	qint64 documentSequence = 0;
	qint64 itemSequence = 0;
	QByteArray data;
	QDataStream stream(&data, QIODevice::ReadWrite);
	stream << int(2) << history.entries.count() << index;

	for (int i = 0; i < history.entries.count(); ++i)
	{
		stream << QString(QUrl::toPercentEncoding(history.entries.at(i).url, QByteArray(":/#?&+=@%*"))) << history.entries.at(i).title << history.entries.at(i).url << quint32(2) << quint64(0) << ++documentSequence << quint64(0) << QString() << false << ++itemSequence << QString() << qint32(history.entries.at(i).position.x()) << qint32(history.entries.at(i).position.y()) << qreal(1) << false << QString() << false;
	}

	stream.device()->reset();
	stream >> *(m_webView->page()->history());

	for (int i = 0; i < history.entries.count(); ++i)
	{
		QVariantList data;
		data.append(-1);
		data.append(history.entries.at(i).zoom);
		data.append(history.entries.at(i).position);

		m_webView->page()->history()->itemAt(i).setUserData(data);
	}

	m_webView->page()->history()->goToItem(m_webView->page()->history()->itemAt(index));

	const QUrl url = m_webView->page()->history()->itemAt(index).url();

	setRequestedUrl(url, false, true);
	updateOptions(url);
	updatePageActions(url);
}

void QtWebKitWebWidget::setHistory(QDataStream &stream)
{
	stream.device()->reset();
	stream >> *(m_webView->page()->history());

	setRequestedUrl(m_webView->page()->history()->currentItem().url(), false, true);
	updateOptions(m_webView->page()->history()->currentItem().url());
}

void QtWebKitWebWidget::setZoom(int zoom)
{
	if (zoom != getZoom())
	{
		m_webView->setZoomFactor(qBound(0.1, ((qreal) zoom / 100), (qreal) 100));

		SessionsManager::markSessionModified();

		emit zoomChanged(zoom);
		emit progressBarGeometryChanged();
	}
}

void QtWebKitWebWidget::setUrl(const QUrl &url, bool typed)
{
	if (url.scheme() == QLatin1String("javascript"))
	{
		m_webView->page()->mainFrame()->evaluateJavaScript(url.toDisplayString(QUrl::RemoveScheme | QUrl::DecodeReserved));

		return;
	}

	if (!url.fragment().isEmpty() && url.matches(getUrl(), (QUrl::RemoveFragment | QUrl::StripTrailingSlash | QUrl::NormalizePathSegments)))
	{
		m_webView->page()->mainFrame()->scrollToAnchor(url.fragment());

		return;
	}

	m_isTyped = typed;

	QUrl targetUrl(url);

	if (url.isValid() && url.scheme().isEmpty() && !url.path().startsWith('/'))
	{
		QUrl httpUrl = url;
		httpUrl.setScheme(QLatin1String("http"));

		targetUrl = httpUrl;
	}
	else if (url.isValid() && (url.scheme().isEmpty() || url.scheme() == "file"))
	{
		QUrl localUrl = url;
		localUrl.setScheme(QLatin1String("file"));

		targetUrl = localUrl;
	}

	updateOptions(targetUrl);

	m_networkManager->resetStatistics();

	m_webView->load(targetUrl);

	notifyTitleChanged();
	notifyIconChanged();
}

void QtWebKitWebWidget::setPermission(const QString &key, const QUrl &url, WebWidget::PermissionPolicies policies)
{
	WebWidget::setPermission(key, url, policies);

	QWebPage::Feature feature = QWebPage::Geolocation;

	if (key == QLatin1String("Browser/EnableGeolocation"))
	{
		feature = QWebPage::Geolocation;
	}
	else if (key == QLatin1String("Browser/EnableNotifications"))
	{
		feature = QWebPage::Notifications;
	}
	else
	{
		return;
	}

	QList<QWebFrame*> frames;
	frames.append(m_page->mainFrame());

	while (!frames.isEmpty())
	{
		QWebFrame *frame = frames.takeFirst();

		if (frame->url() == url)
		{
			m_page->setFeaturePermission(frame, feature, (policies.testFlag(GrantedPermission) ? QWebPage::PermissionGrantedByUser : QWebPage::PermissionDeniedByUser));
		}

		frames.append(frame->childFrames());
	}
}

void QtWebKitWebWidget::setOption(const QString &key, const QVariant &value)
{
	WebWidget::setOption(key, value);

	updateOptions(getUrl());

	if (key == QLatin1String("Content/DefaultCharacterEncoding"))
	{
		m_webView->reload();
	}
}

void QtWebKitWebWidget::setScrollPosition(const QPoint &position)
{
	m_webView->page()->mainFrame()->setScrollPosition(position);
}

void QtWebKitWebWidget::setOptions(const QVariantHash &options)
{
	WebWidget::setOptions(options);

	updateOptions(getUrl());
}

QString QtWebKitWebWidget::getPluginToken() const
{
	return m_pluginToken;
}

WebWidget* QtWebKitWebWidget::clone(bool cloneHistory)
{
	QtWebKitWebWidget *widget = new QtWebKitWebWidget(isPrivate(), getBackend(), m_networkManager->clone(), NULL);
	widget->setOptions(getOptions());

	if (cloneHistory)
	{
		QByteArray data;
		QDataStream stream(&data, QIODevice::ReadWrite);
		stream << *(m_webView->page()->history());

		widget->setHistory(stream);
	}

	widget->setZoom(getZoom());

	return widget;
}

Action* QtWebKitWebWidget::getAction(int identifier)
{
	if (identifier < 0)
	{
		return NULL;
	}

	if (m_actions.contains(identifier))
	{
		return m_actions[identifier];
	}

	Action *action = new Action(identifier, this);

	m_actions[identifier] = action;

	connect(action, SIGNAL(triggered()), this, SLOT(triggerAction()));

	switch (identifier)
	{
		case ActionsManager::InspectPageAction:
		case ActionsManager::InspectElementAction:
		case ActionsManager::FindAction:
		case ActionsManager::FindNextAction:
		case ActionsManager::FindPreviousAction:
			action->setEnabled(true);

			break;
		case ActionsManager::CheckSpellingAction:
		case ActionsManager::ViewSourceAction:
			action->setEnabled(false);

			break;
		case ActionsManager::AddBookmarkAction:
		case ActionsManager::WebsitePreferencesAction:
			updatePageActions(getUrl());

			break;
		case ActionsManager::GoBackAction:
		case ActionsManager::RewindAction:
			action->setEnabled(m_webView->history()->canGoBack());

			break;
		case ActionsManager::GoForwardAction:
		case ActionsManager::FastForwardAction:
			action->setEnabled(m_webView->history()->canGoForward());

			break;
		case ActionsManager::PasteNoteAction:
			action->setMenu(getPasteNoteMenu());

			updateEditActions();

			break;
		case ActionsManager::StopAction:
			action->setEnabled(m_isLoading);

			break;

		case ActionsManager::ReloadAction:
			action->setEnabled(!m_isLoading);

			break;
		case ActionsManager::ReloadOrStopAction:
			action->setup(m_isLoading ? getAction(ActionsManager::StopAction) : getAction(ActionsManager::ReloadAction));

			break;
		case ActionsManager::ScheduleReloadAction:
			action->setMenu(getReloadTimeMenu());

			break;
		case ActionsManager::LoadPluginsAction:
			action->setEnabled(findChildren<QtWebKitPluginWidget*>().count() > 0);

			break;
		case ActionsManager::ValidateAction:
			action->setEnabled(false);
			action->setMenu(new QMenu(this));

			break;
		case ActionsManager::UndoAction:
			action->setEnabled(m_page->undoStack()->canUndo());

			updateUndoText(m_page->undoStack()->undoText());

			connect(m_page->undoStack(), SIGNAL(canUndoChanged(bool)), action, SLOT(setEnabled(bool)));
			connect(m_page->undoStack(), SIGNAL(undoTextChanged(QString)), this, SLOT(updateUndoText(QString)));

			break;
		case ActionsManager::RedoAction:
			action->setEnabled(m_page->undoStack()->canRedo());

			updateRedoText(m_page->undoStack()->redoText());

			connect(m_page->undoStack(), SIGNAL(canRedoChanged(bool)), action, SLOT(setEnabled(bool)));
			connect(m_page->undoStack(), SIGNAL(redoTextChanged(QString)), this, SLOT(updateRedoText(QString)));

			break;
		case ActionsManager::SearchMenuAction:
			action->setMenu(getQuickSearchMenu());

		case ActionsManager::CutAction:
		case ActionsManager::CopyAction:
		case ActionsManager::CopyPlainTextAction:
		case ActionsManager::CopyToNoteAction:
		case ActionsManager::PasteAction:
		case ActionsManager::PasteAndGoAction:
		case ActionsManager::DeleteAction:
		case ActionsManager::ClearAllAction:
		case ActionsManager::SearchAction:
			updateEditActions();

			break;
		case ActionsManager::OpenLinkAction:
		case ActionsManager::OpenLinkInCurrentTabAction:
		case ActionsManager::OpenLinkInNewTabAction:
		case ActionsManager::OpenLinkInNewTabBackgroundAction:
		case ActionsManager::OpenLinkInNewWindowAction:
		case ActionsManager::OpenLinkInNewWindowBackgroundAction:
		case ActionsManager::OpenLinkInNewPrivateTabAction:
		case ActionsManager::OpenLinkInNewPrivateTabBackgroundAction:
		case ActionsManager::OpenLinkInNewPrivateWindowAction:
		case ActionsManager::OpenLinkInNewPrivateWindowBackgroundAction:
		case ActionsManager::CopyLinkToClipboardAction:
		case ActionsManager::BookmarkLinkAction:
		case ActionsManager::SaveLinkToDiskAction:
		case ActionsManager::SaveLinkToDownloadsAction:
			updateLinkActions();

			break;
		case ActionsManager::OpenFrameInCurrentTabAction:
		case ActionsManager::OpenFrameInNewTabAction:
		case ActionsManager::OpenFrameInNewTabBackgroundAction:
		case ActionsManager::CopyFrameLinkToClipboardAction:
		case ActionsManager::ReloadFrameAction:
		case ActionsManager::ViewFrameSourceAction:
			updateFrameActions();

			break;
		case ActionsManager::OpenImageInNewTabAction:
		case ActionsManager::SaveImageToDiskAction:
		case ActionsManager::CopyImageToClipboardAction:
		case ActionsManager::CopyImageUrlToClipboardAction:
		case ActionsManager::ReloadImageAction:
		case ActionsManager::ImagePropertiesAction:
			updateImageActions();

			break;
		case ActionsManager::SaveMediaToDiskAction:
		case ActionsManager::CopyMediaUrlToClipboardAction:
		case ActionsManager::MediaControlsAction:
		case ActionsManager::MediaLoopAction:
		case ActionsManager::MediaPlayPauseAction:
		case ActionsManager::MediaMuteAction:
			updateMediaActions();

			break;
		default:
			break;
	}

	return action;
}

QWebPage* QtWebKitWebWidget::getPage()
{
	return m_webView->page();
}

QString QtWebKitWebWidget::getTitle() const
{
	const QString title = m_webView->title();

	if (title.isEmpty())
	{
		const QUrl url = getUrl();

		if (url.scheme() == QLatin1String("about") && (url.path().isEmpty() || url.path() == QLatin1String("blank") || url.path() == QLatin1String("start")))
		{
			return tr("Blank Page");
		}

		if (url.isLocalFile())
		{
			return QFileInfo(url.toLocalFile()).canonicalFilePath();
		}

		if (!url.isEmpty())
		{
			return url.toString();
		}

		return tr("(Untitled)");
	}

	return title;
}

QString QtWebKitWebWidget::getSelectedText() const
{
	return m_webView->selectedText();
}

QUrl QtWebKitWebWidget::getUrl() const
{
	const QUrl url = m_webView->url();

	return (url.isEmpty() ? m_webView->page()->mainFrame()->requestedUrl() : url);
}

QIcon QtWebKitWebWidget::getIcon() const
{
	if (isPrivate())
	{
		return Utils::getIcon(QLatin1String("tab-private"));
	}

	const QIcon icon = m_webView->icon();

	return (icon.isNull() ? Utils::getIcon(QLatin1String("tab")) : icon);
}

QPixmap QtWebKitWebWidget::getThumbnail()
{
	if (!m_thumbnail.isNull() && !isLoading())
	{
		return m_thumbnail;
	}

	const QSize thumbnailSize = QSize(260, 170);
	const QSize oldViewportSize = m_webView->page()->viewportSize();
	const QPoint position = m_webView->page()->mainFrame()->scrollPosition();
	const qreal zoom = m_webView->page()->mainFrame()->zoomFactor();
	QSize contentsSize = m_webView->page()->mainFrame()->contentsSize();
	QWidget *newView = new QWidget();
	QWidget *oldView = m_webView->page()->view();

	m_webView->page()->setView(newView);
	m_webView->page()->setViewportSize(contentsSize);
	m_webView->page()->mainFrame()->setZoomFactor(1);

	if (contentsSize.width() > 2000)
	{
		contentsSize.setWidth(2000);
	}

	contentsSize.setHeight(thumbnailSize.height() * (qreal(contentsSize.width()) / thumbnailSize.width()));

	QPixmap pixmap(contentsSize);
	pixmap.fill(Qt::white);

	QPainter painter(&pixmap);

	m_webView->page()->mainFrame()->render(&painter, QWebFrame::ContentsLayer, QRegion(QRect(QPoint(0, 0), contentsSize)));
	m_webView->page()->mainFrame()->setZoomFactor(zoom);
	m_webView->page()->setView(oldView);
	m_webView->page()->setViewportSize(oldViewportSize);
	m_webView->page()->mainFrame()->setScrollPosition(position);

	painter.end();

	pixmap = pixmap.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	newView->deleteLater();

	m_thumbnail = pixmap;

	return pixmap;
}

QPoint QtWebKitWebWidget::getScrollPosition() const
{
	return m_webView->page()->mainFrame()->scrollPosition();
}

QRect QtWebKitWebWidget::getProgressBarGeometry() const
{
	QRect geometry(QPoint(0, (height() - ((m_inspector && m_inspector->isVisible()) ? m_inspector->height() : 0) - 30)), QSize(width(), 30));
	const QRect horizontalScrollBar = m_webView->page()->mainFrame()->scrollBarGeometry(Qt::Horizontal);
	const QRect verticalScrollBar = m_webView->page()->mainFrame()->scrollBarGeometry(Qt::Vertical);

	if (horizontalScrollBar.height() > 0 && geometry.intersects(horizontalScrollBar))
	{
		geometry.moveTop(m_webView->height() - 30 - horizontalScrollBar.height());
	}

	if (verticalScrollBar.width() > 0 && geometry.intersects(verticalScrollBar))
	{
		if (verticalScrollBar.left() == 0)
		{
			geometry.setLeft(verticalScrollBar.right());
		}
		else
		{
			geometry.setRight(verticalScrollBar.left());
		}
	}

	return geometry;
}

WindowHistoryInformation QtWebKitWebWidget::getHistory() const
{
	QVariantList data = m_webView->history()->currentItem().userData().toList();

	if (data.isEmpty() || data.length() < 3)
	{
		data.clear();
		data.append(0);
		data.append(getZoom());
		data.append(m_webView->page()->mainFrame()->scrollPosition());
	}
	else
	{
		data[ZoomEntryData] = getZoom();
		data[PositionEntryData] = m_webView->page()->mainFrame()->scrollPosition();
	}

	m_webView->history()->currentItem().setUserData(data);

	QWebHistory *history = m_webView->history();
	WindowHistoryInformation information;
	information.index = history->currentItemIndex();

	const QString requestedUrl = m_webView->page()->mainFrame()->requestedUrl().toString();
	const int historyCount = history->count();

	for (int i = 0; i < historyCount; ++i)
	{
		const QWebHistoryItem item = history->itemAt(i);
		WindowHistoryEntry entry;
		entry.url = item.url().toString();
		entry.title = item.title();
		entry.position = item.userData().toList().value(PositionEntryData, QPoint(0, 0)).toPoint();
		entry.zoom = item.userData().toList().value(ZoomEntryData).toInt();

		information.entries.append(entry);
	}

	if (isLoading() && requestedUrl != history->itemAt(history->currentItemIndex()).url().toString())
	{
		WindowHistoryEntry entry;
		entry.url = requestedUrl;
		entry.title = getTitle();
		entry.position = data.value(PositionEntryData, QPoint(0, 0)).toPoint();
		entry.zoom = data.value(ZoomEntryData).toInt();

		information.index = historyCount;
		information.entries.append(entry);
	}

	return information;
}

QList<FeedUrl> QtWebKitWebWidget::getFeeds() const
{
	const QWebElementCollection elements = m_webView->page()->mainFrame()->findAllElements(QLatin1String("a[type=\"application/atom+xml\"], a[type=\"application/rss+xml\"], link[type=\"application/atom+xml\"], link[type=\"application/rss+xml\"]"));
	QList<FeedUrl> feeds;
	QSet<QUrl> urls;

	for (int i = 0; i < elements.count(); ++i)
	{
		QUrl url(elements.at(i).attribute(QLatin1String("href")));

		if (url.isRelative())
		{
			url = getUrl().resolved(url);
		}

		if (urls.contains(url))
		{
			continue;
		}

		urls.insert(url);

		FeedUrl feed;
		feed.title = elements.at(i).attribute(QLatin1String("title"));
		feed.mimeType = elements.at(i).attribute(QLatin1String("type"));
		feed.url = url;

		feeds.append(feed);
	}

	return feeds;
}

QHash<QByteArray, QByteArray> QtWebKitWebWidget::getHeaders() const
{
	return m_networkManager->getHeaders();
}

QVariantHash QtWebKitWebWidget::getStatistics() const
{
	return m_networkManager->getStatistics();
}

int QtWebKitWebWidget::getZoom() const
{
	return (m_webView->zoomFactor() * 100);
}

QVector<int> QtWebKitWebWidget::getContentBlockingProfiles() const
{
	return m_contentBlockingProfiles;
}

bool QtWebKitWebWidget::canLoadPlugins() const
{
	return m_canLoadPlugins;
}

bool QtWebKitWebWidget::isScrollBar(const QPoint &position) const
{
	return (m_page->mainFrame()->scrollBarGeometry(Qt::Horizontal).contains(position) || m_page->mainFrame()->scrollBarGeometry(Qt::Vertical).contains(position));
}

bool QtWebKitWebWidget::isLoading() const
{
	return m_isLoading;
}

bool QtWebKitWebWidget::isPrivate() const
{
	return m_webView->settings()->testAttribute(QWebSettings::PrivateBrowsingEnabled);
}

bool QtWebKitWebWidget::findInPage(const QString &text, FindFlags flags)
{
#if QTWEBKIT_VERSION >= 0x050200
	QWebPage::FindFlags nativeFlags = (QWebPage::FindWrapsAroundDocument | QWebPage::FindBeginsInSelection);
#else
	QWebPage::FindFlags nativeFlags = QWebPage::FindWrapsAroundDocument;
#endif

	if (flags & BackwardFind)
	{
		nativeFlags |= QWebPage::FindBackward;
	}

	if (flags & CaseSensitiveFind)
	{
		nativeFlags |= QWebPage::FindCaseSensitively;
	}

	if (flags & HighlightAllFind || text.isEmpty())
	{
		m_webView->findText(QString(), QWebPage::HighlightAllOccurrences);
		m_webView->findText(text, (nativeFlags | QWebPage::HighlightAllOccurrences));
	}

	return m_webView->findText(text, nativeFlags);
}

bool QtWebKitWebWidget::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_webView)
	{
		if (event->type() == QEvent::ContextMenu)
		{
			QContextMenuEvent *contextMenuEvent = static_cast<QContextMenuEvent*>(event);

			m_ignoreContextMenu = (contextMenuEvent->reason() == QContextMenuEvent::Mouse);

			if (contextMenuEvent->reason() == QContextMenuEvent::Keyboard)
			{
				triggerAction(ActionsManager::ContextMenuAction);
			}
		}
		else if (event->type() == QEvent::Resize)
		{
			emit progressBarGeometryChanged();
		}
		else if (event->type() == QEvent::ToolTip)
		{
			const QString toolTipsMode = SettingsManager::getValue(QLatin1String("Browser/ToolTipsMode")).toString();
			const QPoint position = QCursor::pos();
			const QWebHitTestResult result = m_webView->page()->mainFrame()->hitTestContent(m_webView->mapFromGlobal(position));
			QString link = result.linkUrl().toString();
			QString text;

			if (link.isEmpty() && (result.element().tagName().toLower() == QLatin1String("input") || result.element().tagName().toLower() == QLatin1String("button")) && (result.element().attribute(QLatin1String("type")).toLower() == QLatin1String("submit") || result.element().attribute(QLatin1String("type")).toLower() == QLatin1String("image")))
			{
				QWebElement parentElement = result.element().parent();

				while (!parentElement.isNull() && parentElement.tagName().toLower() != QLatin1String("form"))
				{
					parentElement = parentElement.parent();
				}

				if (!parentElement.isNull() && parentElement.hasAttribute(QLatin1String("action")))
				{
					const QUrl url(parentElement.attribute(QLatin1String("action")));

					link = (url.isEmpty() ? getUrl() : (url.isRelative() ? getUrl().resolved(url) : url)).toString();
				}
			}

			if (toolTipsMode != QLatin1String("disabled"))
			{
				const QString title = result.title().replace(QLatin1Char('&'), QLatin1String("&amp;")).replace(QLatin1Char('<'), QLatin1String("&lt;")).replace(QLatin1Char('>'), QLatin1String("&gt;"));

				if (toolTipsMode == QLatin1String("extended"))
				{
					if (!link.isEmpty())
					{
						text = (title.isEmpty() ? QString() : tr("Title: %1").arg(title) + QLatin1String("<br>")) + tr("Address: %1").arg(link);
					}
					else if (!title.isEmpty())
					{
						text = title;
					}
				}
				else
				{
					text = title;
				}
			}

			setStatusMessage((link.isEmpty() ? result.title() : link), true);

			if (!text.isEmpty())
			{
				QToolTip::showText(position, QStringLiteral("<div style=\"white-space:pre-line;\">%1</div>").arg(text), m_webView);
			}

			event->accept();

			return true;
		}
		else if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (mouseEvent->button() == Qt::BackButton)
			{
				triggerAction(ActionsManager::GoBackAction);

				event->accept();

				return true;
			}

			if (mouseEvent->button() == Qt::ForwardButton)
			{
				triggerAction(ActionsManager::GoForwardAction);

				event->accept();

				return true;
			}

			if ((mouseEvent->button() == Qt::LeftButton || mouseEvent->button() == Qt::MiddleButton) && !isScrollBar(mouseEvent->pos()))
			{
				if (mouseEvent->button() == Qt::LeftButton && mouseEvent->buttons().testFlag(Qt::RightButton))
				{
					m_isUsingRockerNavigation = true;

					triggerAction(ActionsManager::GoBackAction);

					return true;
				}

				m_hitResult = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (m_hitResult.linkUrl().isValid())
				{
					if (mouseEvent->button() == Qt::LeftButton && mouseEvent->modifiers() == Qt::NoModifier)
					{
						return false;
					}

					openUrl(m_hitResult.linkUrl(), WindowsManager::calculateOpenHints(mouseEvent->modifiers(), mouseEvent->button(), CurrentTabOpen));

					event->accept();

					return true;
				}

				if (mouseEvent->button() == Qt::MiddleButton)
				{
					const QString tagName = m_hitResult.element().tagName().toLower();

					if (!m_hitResult.linkUrl().isValid() && tagName != QLatin1String("textarea") && tagName != QLatin1String("input"))
					{
						triggerAction(ActionsManager::StartMoveScrollAction);

						return true;
					}
				}
			}
			else if (mouseEvent->button() == Qt::RightButton)
			{
				m_hitResult = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (mouseEvent->buttons().testFlag(Qt::LeftButton))
				{
					triggerAction(ActionsManager::GoForwardAction);

					event->ignore();
				}
				else
				{
					event->accept();

					if (!m_hitResult.linkElement().isNull())
					{
						m_clickPosition = mouseEvent->pos();
					}

					GesturesManager::startGesture((m_hitResult.linkUrl().isValid() ? GesturesManager::LinkGesturesContext : GesturesManager::GenericGesturesContext), m_webView, mouseEvent);
				}

				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonRelease)
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (getScrollMode() == MoveScroll)
			{
				return true;
			}

			if (mouseEvent->button() == Qt::MiddleButton)
			{
				m_hitResult = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (getScrollMode() == DragScroll)
				{
					triggerAction(ActionsManager::EndScrollAction);
				}
				else if (m_hitResult.linkUrl().isValid())
				{
					return true;
				}
			}
			else if (mouseEvent->button() == Qt::LeftButton && SettingsManager::getValue(QLatin1String("Browser/EnablePlugins"), getUrl()).toString() == QLatin1String("onDemand"))
			{
				QWidget *widget = childAt(mouseEvent->pos());

				m_hitResult = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (widget && widget->metaObject()->className() == QLatin1String("Otter::QtWebKitPluginWidget") && (m_hitResult.element().tagName().toLower() == QLatin1String("object") || m_hitResult.element().tagName().toLower() == QLatin1String("embed")))
				{
					m_pluginToken = QUuid::createUuid().toString();

					m_hitResult.element().setAttribute(QLatin1String("data-otter-browser"), m_pluginToken);

					QWebElement element = m_hitResult.element().clone();

					m_hitResult.element().replace(element);

					element.removeAttribute(QLatin1String("data-otter-browser"));

					if (m_actions.contains(ActionsManager::LoadPluginsAction))
					{
						getAction(ActionsManager::LoadPluginsAction)->setEnabled(findChildren<QtWebKitPluginWidget*>().count() > 0);
					}
				}
			}
			else if (mouseEvent->button() == Qt::RightButton && !mouseEvent->buttons().testFlag(Qt::LeftButton))
			{
				if (m_isUsingRockerNavigation)
				{
					m_isUsingRockerNavigation = false;
					m_ignoreContextMenuNextTime = true;

					QMouseEvent mousePressEvent(QEvent::MouseButtonPress, QPointF(mouseEvent->pos()), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
					QMouseEvent mouseReleaseEvent(QEvent::MouseButtonRelease, QPointF(mouseEvent->pos()), Qt::RightButton, Qt::RightButton, Qt::NoModifier);

					QCoreApplication::sendEvent(m_webView, &mousePressEvent);
					QCoreApplication::sendEvent(m_webView, &mouseReleaseEvent);
				}
				else
				{
					m_ignoreContextMenu = false;

					if (!GesturesManager::endGesture(m_webView, mouseEvent))
					{
						if (m_ignoreContextMenuNextTime)
						{
							m_ignoreContextMenuNextTime = false;

							event->ignore();

							return false;
						}

						showContextMenu(mouseEvent->pos());
					}
				}

				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonDblClick && SettingsManager::getValue(QLatin1String("Browser/ShowSelectionContextMenuOnDoubleClick")).toBool())
		{
			QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

			if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
			{
				m_hitResult = m_webView->page()->mainFrame()->hitTestContent(mouseEvent->pos());

				if (!m_hitResult.isContentEditable() && m_hitResult.element().tagName().toLower() != QLatin1String("textarea") && m_hitResult.element().tagName().toLower() != QLatin1String("select") && m_hitResult.element().tagName().toLower() != QLatin1String("input"))
				{
					m_clickPosition = mouseEvent->pos();

					QTimer::singleShot(250, this, SLOT(showContextMenu()));
				}
			}
		}
		else if (event->type() == QEvent::Wheel)
		{
			if (getScrollMode() == MoveScroll)
			{
				triggerAction(ActionsManager::EndScrollAction);

				return true;
			}
			else
			{
				QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);

				if (wheelEvent->buttons() == Qt::RightButton)
				{
					m_ignoreContextMenuNextTime = true;

					event->ignore();

					return false;
				}

				if (wheelEvent->modifiers().testFlag(Qt::ControlModifier))
				{
					setZoom(getZoom() + (wheelEvent->delta() / 16));

					event->accept();

					return true;
				}
			}
		}
		else if (event->type() == QEvent::ShortcutOverride)
		{
			QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

			if (keyEvent->modifiers() == Qt::ControlModifier)
			{
				if (keyEvent->key() == Qt::Key_Backspace && m_page->currentFrame()->hitTestContent(m_page->inputMethodQuery(Qt::ImCursorRectangle).toRect().center()).isContentEditable())
				{
					event->accept();
				}

				return true;
			}

			if ((keyEvent->modifiers().testFlag(Qt::AltModifier) || keyEvent->modifiers().testFlag(Qt::GroupSwitchModifier)) && m_page->currentFrame()->hitTestContent(m_page->inputMethodQuery(Qt::ImCursorRectangle).toRect().center()).isContentEditable())
			{
				event->accept();

				return true;
			}
		}
	}
	else if (object == m_inspector && (event->type() == QEvent::Move || event->type() == QEvent::Resize) && m_inspectorCloseButton)
	{
		m_inspectorCloseButton->move(QPoint((m_inspector->width() - 19), 3));
	}

	return QObject::eventFilter(object, event);
}

}
