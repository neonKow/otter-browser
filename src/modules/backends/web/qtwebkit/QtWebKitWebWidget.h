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

#ifndef OTTER_QTWEBKITWEBWIDGET_H
#define OTTER_QTWEBKITWEBWIDGET_H

#include "../../../../ui/WebWidget.h"

#include <QtWebKitWidgets/QWebHitTestResult>
#include <QtWebKitWidgets/QWebInspector>
#include <QtWebKitWidgets/QWebPage>
#include <QtWebKitWidgets/QWebView>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QToolButton>

namespace Otter
{

class ContentsDialog;
class QtWebKitNetworkManager;
class QtWebKitWebBackend;
class QtWebKitPage;
class QtWebKitPluginFactory;

class QtWebKitWebWidget : public WebWidget
{
	Q_OBJECT

public:
	~QtWebKitWebWidget();

	void search(const QString &query, const QString &engine);
	void print(QPrinter *printer);
	WebWidget* clone(bool cloneHistory = true);
	Action* getAction(int identifier);
	QString getDefaultCharacterEncoding() const;
	QString getTitle() const;
	QString getSelectedText() const;
	QUrl getUrl() const;
	QIcon getIcon() const;
	QPixmap getThumbnail();
	QPoint getScrollPosition() const;
	QRect getProgressBarGeometry() const;
	WindowHistoryInformation getHistory() const;
	QList<FeedUrl> getFeeds() const;
	QVector<int> getContentBlockingProfiles() const;
	QHash<QByteArray, QByteArray> getHeaders() const;
	QVariantHash getStatistics() const;
	int getZoom() const;
	bool isLoading() const;
	bool isPrivate() const;
	bool findInPage(const QString &text, FindFlags flags = NoFlagsFind);
	bool eventFilter(QObject *object, QEvent *event);

public slots:
	void clearOptions();
	void clearSelection();
	void goToHistoryIndex(int index);
	void triggerAction(int identifier, bool checked = false);
	void setPermission(const QString &key, const QUrl &url, PermissionPolicies policies);
	void setOption(const QString &key, const QVariant &value);
	void setScrollPosition(const QPoint &position);
	void setHistory(const WindowHistoryInformation &history);
	void setZoom(int zoom);
	void setUrl(const QUrl &url, bool typed = true);

protected:
	enum HistoryEntryData
	{
		IdentifierEntryData = 0,
		ZoomEntryData = 1,
		PositionEntryData = 2
	};

	explicit QtWebKitWebWidget(bool isPrivate, WebBackend *backend, QtWebKitNetworkManager *networkManager, ContentsWidget *parent = NULL);

	void focusInEvent(QFocusEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void clearPluginToken();
	void openUrl(const QUrl &url, OpenHints hints = DefaultOpen);
	void openRequest(const QUrl &url, QNetworkAccessManager::Operation operation, QIODevice *outgoingData);
	void openFormRequest(const QUrl &url, QNetworkAccessManager::Operation operation, QIODevice *outgoingData);
	void pasteText(const QString &text);
	void handleHistory();
	void setHistory(QDataStream &stream);
	void setOptions(const QVariantHash &options);
	QString getPluginToken() const;
	QWebPage* getPage();
	bool canLoadPlugins() const;
	bool isScrollBar(const QPoint &position) const;

protected slots:
	void optionChanged(const QString &option, const QVariant &value);
	void navigating(QWebFrame *frame, QWebPage::NavigationType type);
	void pageLoadStarted();
	void pageLoadFinished();
	void downloadFile(const QNetworkRequest &request);
	void downloadFile(QNetworkReply *reply);
	void saveState(QWebFrame *frame, QWebHistoryItem *item);
	void restoreState(QWebFrame *frame);
	void hideInspector();
	void linkHovered(const QString &link);
	void openFormRequest();
	void handlePrintRequest(QWebFrame *frame);
	void handleWindowCloseRequest();
	void handlePermissionRequest(QWebFrame *frame, QWebPage::Feature feature);
	void handlePermissionCancel(QWebFrame *frame, QWebPage::Feature feature);
	void notifyTitleChanged();
	void notifyUrlChanged(const QUrl &url);
	void notifyIconChanged();
	void notifyPermissionRequested(QWebFrame *frame, QWebPage::Feature feature, bool cancel);
	void updateUndoText(const QString &text);
	void updateRedoText(const QString &text);
	void updatePageActions(const QUrl &url);
	void updateNavigationActions();
	void updateEditActions();
	void updateLinkActions();
	void updateFrameActions();
	void updateImageActions();
	void updateMediaActions();
	void updateBookmarkActions();
	void updateOptions(const QUrl &url);
	void showContextMenu(const QPoint &position = QPoint());

private:
	QWebView *m_webView;
	QtWebKitPage *m_page;
	QtWebKitPluginFactory *m_pluginFactory;
	QWebInspector *m_inspector;
	QToolButton *m_inspectorCloseButton;
	QtWebKitNetworkManager *m_networkManager;
	QSplitter *m_splitter;
	QString m_pluginToken;
	QPixmap m_thumbnail;
	QPoint m_clickPosition;
	QWebHitTestResult m_hitResult;
	QUrl m_formRequestUrl;
	QByteArray m_formRequestBody;
	QVector<int> m_contentBlockingProfiles;
	QHash<int, Action*> m_actions;
	QNetworkAccessManager::Operation m_formRequestOperation;
	bool m_canLoadPlugins;
	bool m_ignoreContextMenu;
	bool m_ignoreContextMenuNextTime;
	bool m_isUsingRockerNavigation;
	bool m_isLoading;
	bool m_isTyped;

friend class QtWebKitNetworkManager;
friend class QtWebKitPluginFactory;
friend class QtWebKitWebBackend;
friend class QtWebKitPage;
};

}

#endif
