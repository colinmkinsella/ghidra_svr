#include "ui/ProjectPanel.h"
#include "ui/ConnectDialog.h"
#include "GhidraConnection.h"
#include "SyncEngine.h"

#include <binaryninjaapi.h>
#include <ui/viewframe.h>

#include <ui/filecontext.h>
#include <ui/uicontext.h>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProjectPanel::ProjectPanel(QWidget* parent) : SidebarWidget("Ghidra") {
    (void)parent;
    buildUi();

    // Register for change events from the singleton connection.
    // The lambda is called from the bridge receive thread — use QueuedConnection
    // to safely marshal the update onto the Qt main thread.
    GhidraConnection::instance().setEventHandler([this](GhidraEvent evt) {
        QMetaObject::invokeMethod(this, [this, e = std::move(evt)]() mutable {
            onEventReceived(std::move(e));
        }, Qt::QueuedConnection);
    });

    updateConnectionButtons(false);
}

ProjectPanel::~ProjectPanel() {
    // Clear the event handler so the singleton doesn't hold a dangling reference.
    GhidraConnection::instance().setEventHandler(nullptr);
}

// ---------------------------------------------------------------------------
// UI layout
// ---------------------------------------------------------------------------

void ProjectPanel::buildUi() {
    // ---- Connection row ----------------------------------------------------
    m_statusLabel = new QLabel("Not connected", this);
    m_statusLabel->setStyleSheet("color: gray;");

    m_connectBtn    = new QPushButton("Connect…",    this);
    m_disconnectBtn = new QPushButton("Disconnect",  this);
    m_refreshBtn    = new QPushButton("Refresh",     this);
    m_refreshBtn->setToolTip("Reload repository list from the Ghidra server");
    m_refreshBtn->setEnabled(false);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(m_statusLabel, /*stretch=*/1);
    topRow->addWidget(m_connectBtn);
    topRow->addWidget(m_disconnectBtn);
    topRow->addWidget(m_refreshBtn);

    // ---- Repository tree ---------------------------------------------------
    m_repoTree = new QTreeWidget(this);
    m_repoTree->setHeaderLabel("Repository");
    m_repoTree->setAnimated(true);

    // ---- Root layout -------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(topRow);
    layout->addWidget(m_repoTree, 1);

    // ---- Signals -----------------------------------------------------------
    connect(m_connectBtn,    &QPushButton::clicked, this, &ProjectPanel::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &ProjectPanel::onDisconnectClicked);
    connect(m_refreshBtn,    &QPushButton::clicked, this, &ProjectPanel::onRefreshClicked);
    connect(m_repoTree, &QTreeWidget::itemExpanded,
            this, &ProjectPanel::onRepoItemExpanded);
    connect(m_repoTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectPanel::onRepoItemDoubleClicked);
    m_repoTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_repoTree, &QTreeWidget::customContextMenuRequested,
            this, &ProjectPanel::onTreeContextMenu);
}

// ---------------------------------------------------------------------------
// Connection slots
// ---------------------------------------------------------------------------

void ProjectPanel::onConnectClicked() {
    ConnectDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto& conn = GhidraConnection::instance();

    // Run the blocking connect call on a background worker thread.
    QString host     = dlg.host();
    int     port     = dlg.port();
    QString user     = dlg.user();
    QString password = dlg.password();

    m_statusLabel->setText("Connecting…");
    m_connectBtn->setEnabled(false);

    BinaryNinja::WorkerEnqueue([this, host, port, user, password]() mutable {
        std::string err;
        bool ok = GhidraConnection::instance().connectToServer(
            host.toStdString(), port,
            user.toStdString(), password.toStdString(), err);

        QMetaObject::invokeMethod(this, [this, ok, errStr = QString::fromStdString(err),
                                         user]() {
            if (ok) {
                m_statusLabel->setText("Connected as " + user);
                m_statusLabel->setStyleSheet("color: green;");
                updateConnectionButtons(true);
                refreshStatus();
            } else {
                m_statusLabel->setText("Connection failed");
                m_statusLabel->setStyleSheet("color: red;");
                updateConnectionButtons(false);
                logError(errStr);
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::onTreeContextMenu(const QPoint& pos) {
    auto* item = m_repoTree->itemAt(pos);
    if (!item || item->data(0, Qt::UserRole).toString() != "item") return;

    QString repo   = item->data(0, Qt::UserRole + 1).toString();
    QString folder = item->data(0, Qt::UserRole + 2).toString();
    QString name   = item->data(0, Qt::UserRole + 3).toString();

    auto& conn = GhidraConnection::instance();
    bool isOurs = conn.isCheckinItem(repo.toStdString(), folder.toStdString(),
                                     name.toStdString());

    QMenu menu(this);
    if (isOurs) {
        menu.addAction("Check In…", [this]() { doCheckin(); });
    } else {
        auto* checkOutAct = menu.addAction("Check Out…", [this, repo, folder, name]() {
            if (!m_currentView) {
                logError("No binary view is open — open a file in Binary Ninja first.");
                return;
            }
            importItem(repo.toStdString(), folder.toStdString(), name.toStdString());
        });
        checkOutAct->setEnabled(!!m_currentView);
    }

    menu.addSeparator();
    menu.addAction("View History…", [this, repo, folder, name]() {
        showHistory(repo, folder, name);
    });
    menu.addAction("Download Binary…", [this, repo, folder, name]() {
        downloadBinary(repo, folder, name);
    });

    menu.exec(m_repoTree->viewport()->mapToGlobal(pos));
}

void ProjectPanel::doCheckin() {
    if (!m_currentView) { logError("No binary view open"); return; }

    auto preview = GhidraConnection::instance().collectCheckinChanges(m_currentView);
    if (preview.empty()) {
        QMessageBox::information(this, "Check In", "No changes to check in.");
        return;
    }

    // ---- Review dialog -------------------------------------------------------
    QDialog dlg(this);
    dlg.setWindowTitle("Review Changes");
    dlg.resize(640, 420);

    auto* reviewTree = new QTreeWidget(&dlg);
    reviewTree->setHeaderLabels({"Change", "Address"});
    reviewTree->header()->setStretchLastSection(false);
    reviewTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    reviewTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    if (!preview.renames.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Symbol renames (%1)").arg(preview.renames.size())});
        node->setExpanded(true);
        for (const auto& r : preview.renames) {
            new QTreeWidgetItem(node, QStringList{
                QString::fromStdString(r.originalName) + "  →  " +
                    QString::fromStdString(r.newName),
                QString("0x%1").arg(r.addr, 0, 16)
            });
        }
    }

    if (!preview.comments.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Comment updates (%1)").arg(preview.comments.size())});
        node->setExpanded(preview.comments.size() <= 20);
        for (const auto& c : preview.comments) {
            QString txt = QString::fromStdString(c.text);
            if (txt.length() > 80) txt = txt.left(77) + "…";
            new QTreeWidgetItem(node, QStringList{
                txt,
                QString("0x%1").arg(c.addr, 0, 16)
            });
        }
    }

    auto* summary = new QLabel(
        QString("%1 symbol rename(s),  %2 comment update(s)")
            .arg(preview.renames.size()).arg(preview.comments.size()), &dlg);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* dlgLayout = new QVBoxLayout(&dlg);
    dlgLayout->addWidget(summary);
    dlgLayout->addWidget(reviewTree, 1);
    dlgLayout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    // ---- Version comment -----------------------------------------------------
    bool ok;
    QString comment = QInputDialog::getText(
        this, "Check In to Ghidra", "Version comment:",
        QLineEdit::Normal, "BN annotations sync", &ok);
    if (!ok || comment.trimmed().isEmpty()) return;

    BinaryViewRef view = m_currentView;
    std::string commentStr = comment.toStdString();
    // Capture item coords before the lambda runs on another thread.
    std::string ciRepo   = GhidraConnection::instance().checkinRepo();
    std::string ciFolder = GhidraConnection::instance().checkinFolder();
    std::string ciItem   = GhidraConnection::instance().checkinItem();

    BinaryNinja::WorkerEnqueue([this, view, commentStr, ciRepo, ciFolder, ciItem,
                                 previewCopy = std::move(preview)]() mutable {
        std::string err;
        bool success = GhidraConnection::instance().checkin(
            view, previewCopy, commentStr, err);

        QMetaObject::invokeMethod(this, [this, success,
                                          errStr  = QString::fromStdString(err),
                                          qRepo   = QString::fromStdString(ciRepo),
                                          qFolder = QString::fromStdString(ciFolder),
                                          qName   = QString::fromStdString(ciItem)]() {
            if (success) {
                addActivityEntry("Check-in complete.");
                onRefreshClicked();
            } else {
                logError("Check-in failed: " + errStr);
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::showHistory(const QString& repo, const QString& folder, const QString& name) {
    // Show a non-modal dialog immediately; populate it once the background fetch completes.
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString("History — %1").arg(name));
    dlg->resize(700, 380);

    auto* table = new QTableWidget(0, 4, dlg);
    table->setHorizontalHeaderLabels({"Version", "Date / Time", "User", "Comment"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);

    auto* status = new QLabel("Loading…", dlg);
    auto* closeBtn = new QPushButton("Close", dlg);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);

    auto* layout = new QVBoxLayout(dlg);
    layout->addWidget(table, 1);
    layout->addWidget(status);
    layout->addWidget(closeBtn);

    dlg->show();

    std::string repoStr   = repo.toStdString();
    std::string folderStr = folder.toStdString();
    std::string nameStr   = name.toStdString();

    BinaryNinja::WorkerEnqueue([this, dlg, table, status, repoStr, folderStr, nameStr]() {
        std::string err;
        auto versions = GhidraConnection::instance().getVersions(repoStr, folderStr, nameStr, err);

        QMetaObject::invokeMethod(dlg, [dlg, table, status,
                                         versions = std::move(versions),
                                         errStr   = QString::fromStdString(err)]() {
            if (!dlg) return; // dialog was closed before fetch finished
            if (!errStr.isEmpty()) {
                status->setText("Error: " + errStr);
                return;
            }

            table->setRowCount(static_cast<int>(versions.size()));
            for (int row = 0; row < static_cast<int>(versions.size()); ++row) {
                const auto& v = versions[row];
                auto dt = QDateTime::fromMSecsSinceEpoch(v.time);
                table->setItem(row, 0, new QTableWidgetItem(QString::number(v.version)));
                table->setItem(row, 1, new QTableWidgetItem(dt.toString("yyyy-MM-dd  hh:mm:ss")));
                table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(v.user)));
                table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(v.comment)));
            }
            // Newest version first.
            table->sortItems(0, Qt::DescendingOrder);
            status->setText(QString("%1 version(s)").arg(versions.size()));
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::downloadBinary(const QString& repo, const QString& folder, const QString& name) {
    // Ask for a save path before hitting the server (fast, stays on UI thread).
    QString suggested = name;
    QString savePath = QFileDialog::getSaveFileName(
        this, "Download Binary — " + name, suggested, "All Files (*)");
    if (savePath.isEmpty()) return;

    std::string repoStr   = repo.toStdString();
    std::string folderStr = folder.toStdString();
    std::string nameStr   = name.toStdString();
    std::string pathStr   = savePath.toStdString();

    BinaryNinja::WorkerEnqueue([this, repoStr, folderStr, nameStr, savePath, pathStr]() {
        std::string err;
        auto files = GhidraConnection::instance().downloadBinary(
            repoStr, folderStr, nameStr, -1, err);

        QMetaObject::invokeMethod(this, [this, files = std::move(files),
                                          errStr = QString::fromStdString(err),
                                          savePath, pathStr]() mutable {
            if (!errStr.isEmpty()) {
                logError("Download failed: " + errStr);
                return;
            }
            if (files.empty()) {
                QMessageBox::warning(this, "Download Binary",
                    "No file bytes found in the Ghidra database.\n"
                    "The binary may not have been imported with file bytes stored.");
                return;
            }

            // If there are multiple source files, let the user pick one.
            const GhidraConnection::BinaryFile* chosen = &files[0];
            if (files.size() > 1) {
                QStringList labels;
                for (const auto& f : files)
                    labels << QString("%1  (%2 bytes)")
                              .arg(QString::fromStdString(f.filename))
                              .arg(f.bytes.size());
                bool ok;
                QString sel = QInputDialog::getItem(
                    this, "Multiple Source Files",
                    "This database contains multiple source files. Select one to download:",
                    labels, 0, false, &ok);
                if (!ok) return;
                int idx = labels.indexOf(sel);
                if (idx >= 0 && idx < (int)files.size())
                    chosen = &files[idx];
            }

            // Write the bytes to disk.
            QFile f(savePath);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::critical(this, "Download Binary",
                    "Could not write to:\n" + savePath + "\n\n" + f.errorString());
                return;
            }
            f.write(reinterpret_cast<const char*>(chosen->bytes.data()),
                    static_cast<qint64>(chosen->bytes.size()));
            f.close();

            // Open the saved file in Binary Ninja.
            auto* ctx = UIContext::activeContext();
            if (ctx) {
                ctx->openFilename(savePath);
            } else {
                QMessageBox::information(this, "Download Complete",
                    QString("Saved %1 bytes to:\n%2")
                        .arg(chosen->bytes.size()).arg(savePath));
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::onRefreshClicked() {
    // Remember which top-level repos were expanded so we can restore them.
    QStringList expandedRepos;
    for (int i = 0; i < m_repoTree->topLevelItemCount(); ++i) {
        auto* item = m_repoTree->topLevelItem(i);
        if (item->isExpanded())
            expandedRepos << item->data(0, Qt::UserRole + 1).toString();
    }

    m_refreshBtn->setEnabled(false);
    addActivityEntry("Refreshing repository list…");
    BinaryNinja::WorkerEnqueue([this, expandedRepos]() {
        std::string err;
        auto repos = GhidraConnection::instance().listRepos(err);
        QMetaObject::invokeMethod(this, [this, repos, expandedRepos,
                                          errStr = QString::fromStdString(err)]() {
            m_refreshBtn->setEnabled(true);
            if (!errStr.isEmpty()) { logError(errStr); return; }
            populateRepos(repos);
            // Re-expand repos that were open — triggers lazy content fetch.
            for (int i = 0; i < m_repoTree->topLevelItemCount(); ++i) {
                auto* item = m_repoTree->topLevelItem(i);
                if (expandedRepos.contains(item->data(0, Qt::UserRole + 1).toString()))
                    item->setExpanded(true);
            }
            addActivityEntry("Refresh complete.");
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::onDisconnectClicked() {
    BinaryNinja::WorkerEnqueue([this]() {
        GhidraConnection::instance().disconnectFromServer();
        QMetaObject::invokeMethod(this, [this]() {
            m_repoTree->clear();
            m_statusLabel->setText("Disconnected");
            m_statusLabel->setStyleSheet("color: gray;");
            updateConnectionButtons(false);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Status refresh
// ---------------------------------------------------------------------------

void ProjectPanel::refreshStatus() {
    BinaryNinja::WorkerEnqueue([this]() {
        std::string err;
        auto repos = GhidraConnection::instance().listRepos(err);
        QMetaObject::invokeMethod(this, [this, repos, errStr = QString::fromStdString(err)]() {
            if (!errStr.isEmpty()) { logError(errStr); return; }
            populateRepos(repos);
            for (int i = 0; i < m_repoTree->topLevelItemCount(); ++i)
                m_repoTree->topLevelItem(i)->setExpanded(true);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Tree population
// ---------------------------------------------------------------------------

void ProjectPanel::populateRepos(const std::vector<std::string>& repos) {
    m_repoTree->clear();
    for (const auto& name : repos) {
        auto* item = new QTreeWidgetItem(m_repoTree, QStringList{QString::fromStdString(name)});
        item->setData(0, Qt::UserRole,     "repo");
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(name));
        // Add a dummy child so the expand arrow appears.
        new QTreeWidgetItem(item, QStringList{"Loading…"});
    }
}

void ProjectPanel::onRepoItemExpanded(QTreeWidgetItem* item) {
    QString kind = item->data(0, Qt::UserRole).toString();
    QString repo = item->data(0, Qt::UserRole + 1).toString();
    QString folder = item->data(0, Qt::UserRole + 2).toString();
    if (folder.isEmpty()) folder = "/";

    // Only expand if still showing the placeholder "Loading…" child.
    if (item->childCount() == 1 && item->child(0)->text(0) == "Loading…") {
        item->takeChildren();
        expandFolder(item, repo.toStdString(), folder.toStdString());
    }
}

void ProjectPanel::expandFolder(QTreeWidgetItem* parent,
                                  const std::string& repo,
                                  const std::string& folder) {
    // Open repo if needed (idempotent).
    BinaryNinja::WorkerEnqueue([this, parent, repo, folder]() {
        auto& conn = GhidraConnection::instance();
        std::string err;
        conn.openRepo(repo, err);

        auto subfolders = conn.getSubfolders(repo, folder, err);
        auto items      = conn.listItems(repo, folder, err);

        QMetaObject::invokeMethod(this, [this, parent, repo, folder,
                                         subfolders, items,
                                         errStr = QString::fromStdString(err)]() {
            if (!errStr.isEmpty()) { logError(errStr); return; }

            if (subfolders.empty() && items.empty()) {
                addActivityEntry(QString("(folder '%1' in '%2' is empty — is the file added to version control on the server?)")
                    .arg(QString::fromStdString(folder))
                    .arg(QString::fromStdString(repo)));
                return;
            }

            for (const auto& sf : subfolders) {
                auto* node = new QTreeWidgetItem(parent,
                    QStringList{QString::fromStdString(sf)});
                node->setData(0, Qt::UserRole,     "folder");
                node->setData(0, Qt::UserRole + 1, parent->data(0, Qt::UserRole + 1));
                QString path = parent->data(0, Qt::UserRole + 2).toString();
                if (path.isEmpty()) path = "/";
                node->setData(0, Qt::UserRole + 2, path + QString::fromStdString(sf) + "/");
                new QTreeWidgetItem(node, QStringList{"Loading…"});
            }

            for (const auto& ri : items) {
                QString label = QString::fromStdString(ri.name);
                if (ri.version >= 0)
                    label += QString(" [v%1]").arg(ri.version);
                auto* node = new QTreeWidgetItem(parent, QStringList{label});
                node->setData(0, Qt::UserRole,     "item");
                node->setData(0, Qt::UserRole + 1, parent->data(0, Qt::UserRole + 1));
                node->setData(0, Qt::UserRole + 2, QString::fromStdString(ri.parentPath));
                node->setData(0, Qt::UserRole + 3, QString::fromStdString(ri.name));
                // Make leaf (importable) items visually distinct.
                QFont f = node->font(0);
                f.setBold(true);
                node->setFont(0, f);
                bool ours = GhidraConnection::instance().isCheckinItem(
                    repo, ri.parentPath, ri.name);
                setItemCheckedOut(node, ours);
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::onRepoItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    QString kind = item->data(0, Qt::UserRole).toString();
    if (kind == "repo" || kind == "folder") {
        addActivityEntry("Tip: expand this node with the arrow, then double-click a project file to import it.");
        return;
    }
    if (kind != "item") return;

    QString repo   = item->data(0, Qt::UserRole + 1).toString();
    QString folder = item->data(0, Qt::UserRole + 2).toString();
    QString name   = item->data(0, Qt::UserRole + 3).toString();

    if (!m_currentView) {
        logError("No binary view is open — open a file in Binary Ninja first, then double-click a project item to import.");
        return;
    }

    importItem(repo.toStdString(), folder.toStdString(), name.toStdString());
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ProjectPanel::onEventReceived(GhidraEvent evt) {
    QString msg = QString("[%1] %2 → %3/%4")
        .arg(QString::fromStdString(evt.repo))
        .arg(QString::fromStdString(evt.type))
        .arg(QString::fromStdString(evt.parentPath))
        .arg(QString::fromStdString(evt.name));
    addActivityEntry(msg);

    // If the affected repo is open in the tree, mark affected items stale.
    // A full refresh would re-fetch folder contents — simple approach: just log.
    // TODO: invalidate the specific tree node and reload it lazily.
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ProjectPanel::updateConnectionButtons(bool connected) {
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_refreshBtn->setEnabled(connected);
}

void ProjectPanel::addActivityEntry(const QString& text) {
    BinaryNinja::LogInfo("Ghidra: %s", text.toStdString().c_str());
}

void ProjectPanel::logError(const QString& msg) {
    BinaryNinja::LogWarn("Ghidra: %s", msg.toStdString().c_str());
}

void ProjectPanel::importItem(const std::string& repo,
                               const std::string& folder,
                               const std::string& name) {
    addActivityEntry(QString("Importing %1/%2…").arg(
        QString::fromStdString(folder), QString::fromStdString(name)));

    BinaryViewRef view = m_currentView;

    BinaryNinja::WorkerEnqueue([this, repo, folder, name, view]() {
        std::string err;
        GhidraDbExport data =
            GhidraConnection::instance().openDatabase(repo, folder, name, -1, err);

        if (!err.empty()) {
            QMetaObject::invokeMethod(this, [this, errStr = QString::fromStdString(err)]() {
                logError(errStr);
            }, Qt::QueuedConnection);
            return;
        }

        uint64_t bnBase = view ? view->GetStart() : 0;

        // Surface bridge diagnostics in the activity log.
        for (const auto& line : data.diag) {
            QMetaObject::invokeMethod(this, [this, s = QString::fromStdString(line)]() {
                addActivityEntry("  [diag] " + s);
            }, Qt::QueuedConnection);
        }

        SyncResult result = SyncEngine::applyToView(view, data);

        // Store write-back state before the UI callback (background thread).
        GhidraConnection::instance().storeCheckinState(
            std::move(result.addrToKey),
            std::move(result.addrToOriginalName),
            std::move(result.addrToCommentKey),
            std::move(result.addrToOriginalComment),
            std::move(result.addrToOriginalFuncComment),
            result.imageBase,
            repo, folder, name);

        QMetaObject::invokeMethod(this, [this,
                                          syms    = result.symbolsApplied,
                                          comms   = result.commentsApplied,
                                          flags   = result.flagsApplied,
                                          addrMin = result.addrMin,
                                          addrMax = result.addrMax,
                                          sample  = result.sampleSymbol,
                                          bnBase,
                                          itemName = QString::fromStdString(name),
                                          qRepo    = QString::fromStdString(repo),
                                          qFolder  = QString::fromStdString(folder),
                                          qName    = QString::fromStdString(name)]() {
            addActivityEntry(QString("Import complete: %1 — %2 symbols, %3 comments, %4 flags")
                .arg(itemName).arg(syms).arg(comms).arg(flags));
            if (syms > 0) {
                addActivityEntry(QString("  Ghidra addr range: 0x%1 – 0x%2  (sample: %3)")
                    .arg(addrMin, 0, 16).arg(addrMax, 0, 16)
                    .arg(QString::fromStdString(sample)));
                addActivityEntry(QString("  BN view start: 0x%1").arg(bnBase, 0, 16));
            }
            // Mark the item green — it is now our active checkout.
            if (auto* tItem = findTreeItem(qRepo, qFolder, qName))
                setItemCheckedOut(tItem, true);
        }, Qt::QueuedConnection);
    });
}

QTreeWidgetItem* ProjectPanel::findTreeItem(const QString& repo,
                                             const QString& folder,
                                             const QString& name) const {
    QTreeWidgetItemIterator it(m_repoTree);
    while (*it) {
        auto* item = *it;
        if (item->data(0, Qt::UserRole).toString() == "item" &&
            item->data(0, Qt::UserRole + 1).toString() == repo &&
            item->data(0, Qt::UserRole + 2).toString() == folder &&
            item->data(0, Qt::UserRole + 3).toString() == name)
            return item;
        ++it;
    }
    return nullptr;
}

void ProjectPanel::setItemCheckedOut(QTreeWidgetItem* item, bool checkedOut) {
    // Green = our active checkout; blue = available.
    item->setForeground(0, checkedOut ? QColor(0x44, 0xee, 0x66)
                                      : QColor(0x44, 0xaa, 0xff));
}

void ProjectPanel::notifyViewChanged(ViewFrame* frame) {
    m_currentView = frame ? frame->getCurrentBinaryView() : BinaryViewRef{};
}
