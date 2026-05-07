#pragma once
#include <QString>
#include <ui/sidebarwidget.h>
#include <ui/uitypes.h>
#include "GhidraConnection.h"

// Forward declarations
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class ViewFrame;

/**
 * Sidebar panel: repo browser + live activity feed.
 *
 * Registered as a BinaryNinja::SidebarWidget.  The GhidraConnection is a
 * singleton so all open panels share the same server connection.
 *
 * UI thread safety: GhidraConnection invokes events on the bridge receive
 * thread.  We forward them to the Qt main thread with QueuedConnection so
 * all Qt widget updates happen on the correct thread.
 */
class ProjectPanel : public SidebarWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget* parent = nullptr);
    ~ProjectPanel();

    /** Called by BN when the active view frame changes. */
    void notifyViewChanged(ViewFrame* frame) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onRefreshClicked();
    void onTreeContextMenu(const QPoint& pos);
    void onRepoItemExpanded(QTreeWidgetItem* item);
    void onRepoItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onEventReceived(GhidraEvent evt);
    void refreshStatus();

private:
    QLabel*       m_statusLabel       = nullptr;
    QPushButton*  m_connectBtn        = nullptr;
    QPushButton*  m_disconnectBtn     = nullptr;
    QPushButton*  m_refreshBtn        = nullptr;
    QTreeWidget*  m_repoTree          = nullptr;

    BinaryViewRef m_currentView;

    void buildUi();
    void updateConnectionButtons(bool connected);
    void populateRepos(const std::vector<std::string>& repos);
    void expandFolder(QTreeWidgetItem* item, const std::string& repo,
                      const std::string& folder);
    void importItem(const std::string& repo, const std::string& folder,
                    const std::string& name);
    void doCheckin();
    void showHistory(const QString& repo, const QString& folder, const QString& name);
    void downloadBinary(const QString& repo, const QString& folder, const QString& name);
    QTreeWidgetItem* findTreeItem(const QString& repo, const QString& folder,
                                  const QString& name) const;
    void setItemCheckedOut(QTreeWidgetItem* item, bool checkedOut);
    void addActivityEntry(const QString& text);
    void logError(const QString& msg);
};
