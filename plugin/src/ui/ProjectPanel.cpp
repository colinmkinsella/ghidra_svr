#include "ui/ProjectPanel.h"
#include "ui/ConnectDialog.h"
#include "GhidraConnection.h"
#include "SyncEngine.h"

#include <binaryninjaapi.h>
#include <ui/viewframe.h>

#include <ui/filecontext.h>
#include <ui/uicontext.h>
#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Project helpers (free functions — no class state needed)
// ---------------------------------------------------------------------------

/** Return the BN Project that owns @p view, or nullptr if it is a standalone file. */
static BinaryNinja::Ref<BinaryNinja::Project>
projectForView(BinaryNinja::Ref<BinaryNinja::BinaryView> view)
{
    if (!view) return nullptr;
    auto pf = view->GetFile()->GetProjectFile();
    return pf ? pf->GetProject() : nullptr;
}

/** Return the ProjectFile ID string for @p view, or "" if not in a project. */
static std::string
projectFileId(BinaryNinja::Ref<BinaryNinja::BinaryView> view)
{
    if (!view) return {};
    auto pf = view->GetFile()->GetProjectFile();
    return pf ? pf->GetId() : std::string{};
}

/**
 * Return the active BN Project whether or not a file is currently open.
 * Checks the active binary view first (most specific), then falls back to
 * UIContext::getProject() so this works on the start page / project browser.
 */
static ProjectRef
currentOpenProject(BinaryNinja::Ref<BinaryNinja::BinaryView> view)
{
    if (view) {
        auto pf = view->GetFile()->GetProjectFile();
        if (pf) return pf->GetProject();
    }
    if (auto* ctx = UIContext::activeContext())
        return ctx->getProject();
    return nullptr;
}

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
    unwatchProject();
}

// ---------------------------------------------------------------------------
// Project file watcher
// ---------------------------------------------------------------------------

void ProjectPanel::ProjectFileWatcher::notify() {
    // Called on a BN internal thread — marshal to the Qt main thread.
    QPointer<ProjectPanel> weak = m_panel;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [weak]() {
        if (weak) weak->scheduleRefreshProjectFiles();
    }, Qt::QueuedConnection);
}

void ProjectPanel::watchProject(BinaryNinja::Ref<BinaryNinja::Project> project) {
    // No-op if already watching this exact project.
    if (m_watchedProject && project &&
        (BinaryNinja::Project*)m_watchedProject == (BinaryNinja::Project*)project)
        return;

    unwatchProject();

    if (!project) return;
    m_watchedProject  = project;
    m_projectWatcher  = std::make_unique<ProjectFileWatcher>(this);
    project->RegisterNotification(m_projectWatcher.get());
}

void ProjectPanel::unwatchProject() {
    if (m_watchedProject && m_projectWatcher)
        m_watchedProject->UnregisterNotification(m_projectWatcher.get());
    m_projectWatcher.reset();
    m_watchedProject = nullptr;
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

    // ---- Project files section (hidden until a project is open) ------------
    auto* projLabel = new QLabel("Project Files", this);
    {
        QFont f = projLabel->font();
        f.setBold(true);
        projLabel->setFont(f);
    }

    m_projectTree = new QTreeWidget(this);
    m_projectTree->setHeaderLabels({"File", "Ghidra Link"});
    m_projectTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_projectTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_projectTree->setRootIsDecorated(false);
    m_projectTree->setAlternatingRowColors(true);
    // Constrain height so it doesn't crowd the repo tree; user can resize.
    m_projectTree->setMaximumHeight(160);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);

    auto* projLayout = new QVBoxLayout;
    projLayout->setContentsMargins(0, 0, 0, 0);
    projLayout->setSpacing(2);
    projLayout->addWidget(projLabel);
    projLayout->addWidget(m_projectTree);
    projLayout->addWidget(divider);

    m_projectSection = new QWidget(this);
    m_projectSection->setLayout(projLayout);
    m_projectSection->hide();

    // ---- Repository tree ---------------------------------------------------
    auto* repoLabel = new QLabel("Ghidra Repository", this);
    {
        QFont f = repoLabel->font();
        f.setBold(true);
        repoLabel->setFont(f);
    }

    m_repoTree = new QTreeWidget(this);
    m_repoTree->setHeaderLabel("Repository");
    m_repoTree->setAnimated(true);

    // ---- Root layout -------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(topRow);
    layout->addWidget(m_projectSection);
    layout->addWidget(repoLabel);
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
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectPanel::onProjectItemDoubleClicked);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested,
            this, &ProjectPanel::onProjectContextMenu);
}

// ---------------------------------------------------------------------------
// Connection slots
// ---------------------------------------------------------------------------

void ProjectPanel::onConnectClicked() {
    ConnectDialog dlg(this);
    // If the current .bndb has a saved Ghidra link, pre-fill the dialog.
    if (m_linkedGhidra.valid())
        dlg.preload(m_linkedGhidra.host, m_linkedGhidra.port, m_linkedGhidra.user);
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
    bool linkedHere = m_linkedGhidra.valid()
        && m_linkedGhidra.repo   == repo
        && m_linkedGhidra.folder == folder
        && m_linkedGhidra.item   == name;
    // Show "Check In…" only when we know a checkout is active:
    //   • isOurs   — we went through importItem/checkout and storeCheckinState was called
    //   • m_checkedOut — upload with "keep checkout" was used (no storeCheckinState called)
    // In all other linked states (after check-in, after terminate, after BN restart)
    // show "Check Out…" so the user can start a fresh checkout for the next round.
    bool effectivelyCheckedOut = isOurs || (linkedHere && m_checkedOut);

    // Do NOT parent this to 'this'.  menu.exec() runs a nested Qt event loop,
    // during which BN may destroy the sidebar and call ~ProjectPanel().  If the
    // QMenu were a child of this widget, Qt's deleteChildren() would try to
    // `delete` the stack-allocated menu → crash (pointer-not-allocated abort).
    QMenu menu;
    if (effectivelyCheckedOut) {
        menu.addAction("Check In…", [this]() { doCheckin(); });
        // Always expose a way to release the checkout without checking in.
        // This un-sticks the "no changes but still checked out" deadlock and
        // lets the user start a fresh checkout for the next editing round.
        menu.addAction("Terminate Checkout", [this, repo, folder, name]() {
            auto btn = QMessageBox::question(
                this, "Terminate Checkout",
                QString("Release the checkout of '%1' without checking in?\n\n"
                        "Any un-pushed BN changes will NOT be sent to Ghidra.")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;

            std::string repoS   = repo.toStdString();
            std::string folderS = folder.toStdString();
            std::string nameS   = name.toStdString();
            BinaryNinja::WorkerEnqueue([this, repoS, folderS, nameS]() {
                std::string err;
                auto checkouts = GhidraConnection::instance()
                                     .getCheckouts(repoS, folderS, nameS, err);
                const std::string myUser = GhidraConnection::instance().connectedUser();
                int64_t coId = -1;
                for (const auto& co : checkouts)
                    if (co.user == myUser) { coId = co.id; break; }

                if (coId >= 0)
                    GhidraConnection::instance().terminateCheckout(
                        repoS, folderS, nameS, coId, err);

                GhidraConnection::instance().clearCheckinState();

                QMetaObject::invokeMethod(this, [this, errStr = QString::fromStdString(err)]() {
                    persistCheckedOutState(false);
                    if (!errStr.isEmpty())
                        logError("Terminate checkout failed: " + errStr);
                    else
                        addActivityEntry("Checkout terminated — ready to check out again.");
                    onRefreshClicked();
                }, Qt::QueuedConnection);
            });
        });
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

    menu.addSeparator();
    menu.addAction("Delete from Server…", [this, repo, folder, name]() {
        // Fetch current checkouts before asking the user to confirm, so the
        // dialog can warn if someone (including this user) has the item open.
        addActivityEntry(QString("Checking checkout state for '%1'…").arg(name));
        BinaryNinja::WorkerEnqueue([this, repo, folder, name]() {
            std::string err;
            auto checkouts = GhidraConnection::instance().getCheckouts(
                repo.toStdString(), folder.toStdString(), name.toStdString(), err);

            QMetaObject::invokeMethod(this,
                [this, repo, folder, name,
                 checkouts = std::move(checkouts),
                 coErr = QString::fromStdString(err)]() {

                // Build the confirmation message, including checkout warnings.
                QString msg = QString(
                    "Permanently delete '%1' from the Ghidra server?\n\n"
                    "This removes all versions and cannot be undone.").arg(name);

                if (!coErr.isEmpty()) {
                    msg += QString("\n\n(Could not retrieve checkout info: %1)").arg(coErr);
                } else if (!checkouts.empty()) {
                    msg += "\n\nWarning — this item is currently checked out:";
                    for (const auto& co : checkouts) {
                        msg += QString("\n  • %1  (%2)")
                            .arg(QString::fromStdString(co.user))
                            .arg(QString::fromStdString(co.type));
                    }
                    msg += "\n\nDeleting will forcibly remove these checkouts.";
                }

                auto btn = QMessageBox::question(
                    this, "Delete from Server", msg,
                    QMessageBox::Yes | QMessageBox::Cancel);
                if (btn != QMessageBox::Yes) return;

                BinaryNinja::WorkerEnqueue([this, repo, folder, name]() {
                    std::string err2;
                    bool ok = GhidraConnection::instance().deleteItem(
                        repo.toStdString(), folder.toStdString(), name.toStdString(), err2);
                    QMetaObject::invokeMethod(this, [this, ok, err = QString::fromStdString(err2),
                                                      repo, folder, name]() {
                        if (!ok) {
                            logError("Delete failed: " + err);
                            return;
                        }
                        addActivityEntry(QString("Deleted '%1' from server.").arg(name));

                        // Clear any BN project file Ghidra links that pointed to the deleted item.
                        if (auto project = currentOpenProject(m_currentView)) {
                            std::string repoS   = repo.toStdString();
                            std::string folderS = folder.toStdString();
                            std::string nameS   = name.toStdString();
                            for (const auto& pf : project->GetFiles()) {
                                auto lm = project->QueryMetadata("ghidra.link." + pf->GetId());
                                if (!lm || !lm->IsKeyValueStore()) continue;
                                auto kv = lm->GetKeyValueStore();
                                auto get = [&](const std::string& k) {
                                    auto it = kv.find(k);
                                    return (it != kv.end() && it->second->IsString())
                                               ? it->second->GetString() : "";
                                };
                                if (get("repo") == repoS && get("item") == nameS) {
                                    project->RemoveMetadata("ghidra.link." + pf->GetId());
                                    // If this is the currently open file, clear in-memory link too.
                                    if (m_currentView) {
                                        m_currentView->RemoveMetadata("ghidra.link");
                                        m_linkedGhidra = {};
                                    }
                                    addActivityEntry(
                                        QString("Ghidra link cleared for '%1'.")
                                        .arg(QString::fromStdString(pf->GetName())));
                                }
                            }
                        }

                        refreshStatus();
                        scheduleRefreshProjectFiles();
                    }, Qt::QueuedConnection); // delete result callback
                });                          // delete WorkerEnqueue
            }, Qt::QueuedConnection);        // checkout dialog callback
        });                                  // checkout fetch WorkerEnqueue
    });

    menu.exec(m_repoTree->viewport()->mapToGlobal(pos));
}

void ProjectPanel::doCheckin() {
    if (!m_currentView) { logError("No binary view open"); return; }

    auto& conn = GhidraConnection::instance();

    BinaryNinja::LogInfo("ghidra: doCheckin — hasCheckinState=%d  checkinItem='%s'  "
                         "linkedGhidra.valid=%d  linkedGhidra.item='%s'  "
                         "m_checkedOut=%d  serverConnected=%d",
        (int)conn.hasCheckinState(),
        conn.checkinItem().c_str(),
        (int)m_linkedGhidra.valid(),
        m_linkedGhidra.item.toStdString().c_str(),
        (int)m_checkedOut,
        (int)conn.isServerConnected());

    // If the check-in state was lost (BN restarted between the last import and
    // now), rebuild the address↔key maps from the server's current symbol table
    // without re-applying anything to the view.  This preserves the user's BN
    // renames while still giving collectCheckinChanges() the baseline it needs.
    if (!conn.hasCheckinState() && m_linkedGhidra.valid()) {
        if (!conn.isServerConnected()) {
            logError("Not connected to the Ghidra server — connect first, then try Check In again.");
            return;
        }

        addActivityEntry(QString("Restoring check-in state for '%1'…").arg(m_linkedGhidra.item));

        std::string repo   = m_linkedGhidra.repo.toStdString();
        std::string folder = m_linkedGhidra.folder.toStdString();
        std::string item   = m_linkedGhidra.item.toStdString();
        BinaryViewRef view = m_currentView;

        BinaryNinja::WorkerEnqueue([this, repo, folder, item, view]() {
            std::string err;
            GhidraDbExport data =
                GhidraConnection::instance().openDatabase(repo, folder, item, -1, err);

            if (!err.empty()) {
                QMetaObject::invokeMethod(this,
                    [this, errStr = QString::fromStdString(err)]() {
                        logError("Could not restore check-in state: " + errStr);
                    }, Qt::QueuedConnection);
                return;
            }

            // Build address↔key maps WITHOUT modifying the view so that any
            // user renames made since the last import are preserved.
            SyncResult maps = SyncEngine::buildWritebackMaps(view, data);

            GhidraConnection::instance().storeCheckinState(
                std::move(maps.addrToKey),
                std::move(maps.addrToOriginalName),
                std::move(maps.addrToCommentKey),
                std::move(maps.addrToOriginalComment),
                std::move(maps.addrToOriginalFuncComment),
                std::move(maps.addrToCommentField),
                std::move(maps.ghidraDataTypes),
                std::move(maps.parameters),
                std::move(maps.bookmarks),
                std::move(maps.dataItems),
                std::move(maps.equates),
                std::move(maps.funcSigs),
                maps.imageBase,
                repo, folder, item);

            // State is now ready — run the review dialog on the main thread.
            QMetaObject::invokeMethod(this, [this]() {
                doCheckinWithState();
            }, Qt::QueuedConnection);
        });
        return; // background restore in progress; doCheckinWithState() will be called later
    }

    doCheckinWithState();
}

void ProjectPanel::doCheckinWithState() {
    if (!m_currentView) { logError("No binary view open"); return; }

    auto& conn   = GhidraConnection::instance();
    auto preview = conn.collectCheckinChanges(m_currentView);

    BinaryNinja::LogInfo("ghidra: doCheckinWithState — checkinItem='%s'  "
                         "renames=%zu  newSyms=%zu  comments=%zu  dataTypes=%zu  "
                         "dataItems=%zu  params=%zu  newParams=%zu  funcSigs=%zu  "
                         "bookmarks=%zu  equateRenames=%zu  newEquateRefs=%zu  "
                         "preview.empty=%d",
        conn.checkinItem().c_str(),
        preview.renames.size(), preview.newSymbols.size(), preview.comments.size(),
        preview.dataTypeChanges.size(), preview.dataItemChanges.size(),
        preview.paramRenames.size(), preview.newParams.size(),
        preview.funcSigChanges.size(), preview.bookmarkChanges.size(),
        preview.equateRenames.size(), preview.newEquateRefs.size(),
        (int)preview.empty());

    if (preview.empty()) {
        // If the item is currently checked out by us, offer to terminate the checkout
        // so the user isn't stuck — they can check back out for the next editing round.
        if (conn.hasCheckinState()) {
            auto btn = QMessageBox::question(
                this, "Check In",
                "No local changes to check in.\n\n"
                "The item may still be checked out on the Ghidra server.\n"
                "Terminate the checkout to release it?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn == QMessageBox::Yes) {
                std::string repo   = conn.checkinRepo();
                std::string folder = conn.checkinFolder();
                std::string item   = conn.checkinItem();
                BinaryNinja::WorkerEnqueue([this, repo, folder, item]() {
                    std::string err;
                    auto checkouts = GhidraConnection::instance()
                                         .getCheckouts(repo, folder, item, err);
                    const std::string myUser = GhidraConnection::instance().connectedUser();
                    int64_t coId = -1;
                    for (const auto& co : checkouts)
                        if (co.user == myUser) { coId = co.id; break; }

                    if (coId >= 0)
                        GhidraConnection::instance().terminateCheckout(
                            repo, folder, item, coId, err);

                    GhidraConnection::instance().clearCheckinState();

                    QMetaObject::invokeMethod(this, [this, errStr = QString::fromStdString(err)]() {
                        persistCheckedOutState(false);
                        if (!errStr.isEmpty())
                            logError("Terminate checkout failed: " + errStr);
                        else
                            addActivityEntry("Checkout terminated — ready to check out again.");
                        onRefreshClicked();
                    }, Qt::QueuedConnection);
                });
            }
        } else {
            QMessageBox::information(this, "Check In", "No changes to check in.");
        }
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

    if (!preview.dataTypeChanges.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Data type changes (%1)").arg(preview.dataTypeChanges.size())});
        node->setExpanded(true);
        for (const auto& dt : preview.dataTypeChanges) {
            QString label;
            if (dt.op == "add")
                label = QString("[add] %1 %2").arg(QString::fromStdString(dt.kind),
                                                   QString::fromStdString(dt.name));
            else if (dt.op == "update")
                label = QString("[update] %1 %2").arg(QString::fromStdString(dt.kind),
                                                      QString::fromStdString(dt.name));
            else
                label = QString("[%1] %2 %3").arg(QString::fromStdString(dt.op),
                                                  QString::fromStdString(dt.kind),
                                                  QString::fromStdString(dt.name));
            new QTreeWidgetItem(node, QStringList{label, ""});
        }
    }

    if (!preview.newSymbols.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("New symbols (%1)").arg(preview.newSymbols.size())});
        node->setExpanded(preview.newSymbols.size() <= 20);
        for (const auto& s : preview.newSymbols) {
            new QTreeWidgetItem(node, QStringList{
                QString::fromStdString(s.name),
                QString("0x%1").arg(s.addr, 0, 16)
            });
        }
    }

    if (!preview.bookmarkChanges.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Bookmark changes (%1)").arg(preview.bookmarkChanges.size())});
        node->setExpanded(preview.bookmarkChanges.size() <= 20);
        for (const auto& b : preview.bookmarkChanges) {
            QString txt = QString("[%1] %2").arg(QString::fromStdString(b.op),
                                                 QString::fromStdString(b.category));
            new QTreeWidgetItem(node, QStringList{
                txt,
                QString("0x%1").arg(b.addr, 0, 16)
            });
        }
    }

    if (!preview.equateRenames.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Equate renames (%1)").arg(preview.equateRenames.size())});
        node->setExpanded(true);
        for (const auto& e : preview.equateRenames)
            new QTreeWidgetItem(node, QStringList{QString::fromStdString(e.name), ""});
    }

    if (!preview.newEquateRefs.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("New equate bindings (%1)").arg(preview.newEquateRefs.size())});
        node->setExpanded(preview.newEquateRefs.size() <= 20);
        for (const auto& r : preview.newEquateRefs) {
            new QTreeWidgetItem(node, QStringList{
                QString("equate id %1, op %2").arg(r.equateId).arg(r.opIndex),
                QString("0x%1").arg(r.addr, 0, 16)
            });
        }
    }

    if (!preview.paramRenames.empty() || !preview.newParams.empty()) {
        int total = (int)preview.paramRenames.size() + (int)preview.newParams.size();
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Parameter/variable changes (%1)").arg(total)});
        node->setExpanded(total <= 20);
        for (const auto& p : preview.paramRenames) {
            QString detail = QString::fromStdString(p.name);
            if (!p.typeName.empty())
                detail += QString(" (%1)").arg(QString::fromStdString(p.typeName));
            new QTreeWidgetItem(node, QStringList{detail, QString("key %1").arg(p.key)});
        }
        for (const auto& p : preview.newParams) {
            QString detail = QString("[new] %1").arg(QString::fromStdString(p.name));
            if (!p.typeName.empty())
                detail += QString(" (%1)").arg(QString::fromStdString(p.typeName));
            new QTreeWidgetItem(node, QStringList{
                detail,
                QString("0x%1").arg(p.funcAddr, 0, 16)
            });
        }
    }

    if (!preview.funcSigChanges.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Function signature changes (%1)").arg(preview.funcSigChanges.size())});
        node->setExpanded(preview.funcSigChanges.size() <= 20);
        for (const auto& f : preview.funcSigChanges) {
            QStringList parts;
            if (!f.callingConvention.empty())
                parts << QString("cc: %1").arg(QString::fromStdString(f.callingConvention));
            if (!f.returnTypeName.empty())
                parts << QString("ret: %1").arg(QString::fromStdString(f.returnTypeName));
            new QTreeWidgetItem(node, QStringList{
                parts.isEmpty() ? "(signature)" : parts.join(", "),
                QString("key %1").arg(f.key)
            });
        }
    }

    if (!preview.dataItemChanges.empty()) {
        auto* node = new QTreeWidgetItem(reviewTree,
            QStringList{QString("Data item changes (%1)").arg(preview.dataItemChanges.size())});
        node->setExpanded(preview.dataItemChanges.size() <= 20);
        for (const auto& d : preview.dataItemChanges) {
            new QTreeWidgetItem(node, QStringList{
                QString("[%1] %2").arg(QString::fromStdString(d.op),
                                       QString::fromStdString(d.typeName)),
                QString("0x%1").arg(d.addr, 0, 16)
            });
        }
    }

    // Build a compact summary line listing every non-zero category.
    QStringList summaryParts;
    if (!preview.renames.empty())
        summaryParts << QString("%1 rename(s)").arg(preview.renames.size());
    if (!preview.comments.empty())
        summaryParts << QString("%1 comment(s)").arg(preview.comments.size());
    if (!preview.dataTypeChanges.empty())
        summaryParts << QString("%1 type(s)").arg(preview.dataTypeChanges.size());
    if (!preview.newSymbols.empty())
        summaryParts << QString("%1 new symbol(s)").arg(preview.newSymbols.size());
    if (!preview.bookmarkChanges.empty())
        summaryParts << QString("%1 bookmark(s)").arg(preview.bookmarkChanges.size());
    if (!preview.equateRenames.empty())
        summaryParts << QString("%1 equate rename(s)").arg(preview.equateRenames.size());
    if (!preview.newEquateRefs.empty())
        summaryParts << QString("%1 equate binding(s)").arg(preview.newEquateRefs.size());
    if (!preview.paramRenames.empty() || !preview.newParams.empty())
        summaryParts << QString("%1 param/var change(s)")
                            .arg(preview.paramRenames.size() + preview.newParams.size());
    if (!preview.funcSigChanges.empty())
        summaryParts << QString("%1 func sig(s)").arg(preview.funcSigChanges.size());
    if (!preview.dataItemChanges.empty())
        summaryParts << QString("%1 data item(s)").arg(preview.dataItemChanges.size());

    auto* summary = new QLabel(summaryParts.join(",  "), &dlg);

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
        bool success = false;
        try {
            success = GhidraConnection::instance().checkin(
                view, previewCopy, commentStr, err);
        } catch (const std::exception& e) {
            err = std::string("Exception: ") + e.what();
        }

        BinaryNinja::LogInfo("ghidra: checkin bridge call — success=%d  err='%s'",
            (int)success, err.c_str());

        if (success)
            GhidraConnection::instance().clearCheckinState();

        QMetaObject::invokeMethod(this, [this, success,
                                          errStr  = QString::fromStdString(err),
                                          qRepo   = QString::fromStdString(ciRepo),
                                          qFolder = QString::fromStdString(ciFolder),
                                          qName   = QString::fromStdString(ciItem)]() {
            if (success) {
                persistCheckedOutState(false);
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
    // If a BN project is open, add the binary directly into it (stores the
    // Ghidra link and auto-imports symbols on open) — same as double-clicking
    // the item in the repo tree.  Fall back to a raw file-save dialog when no
    // project is open so the user can still grab the bytes standalone.
    if (currentOpenProject(m_currentView)) {
        addItemToProject(repo, folder, name);
        return;
    }

    // ---- No project open — save raw binary to disk --------------------------
    QString savePath = QFileDialog::getSaveFileName(
        this, "Download Binary — " + name, name, "All Files (*)");
    if (savePath.isEmpty()) return;

    std::string repoStr   = repo.toStdString();
    std::string folderStr = folder.toStdString();
    std::string nameStr   = name.toStdString();

    BinaryNinja::WorkerEnqueue([this, repoStr, folderStr, nameStr, savePath]() {
        std::string err;
        auto files = GhidraConnection::instance().downloadBinary(
            repoStr, folderStr, nameStr, -1, err);

        QMetaObject::invokeMethod(this, [this, files = std::move(files),
                                          errStr = QString::fromStdString(err),
                                          savePath]() mutable {
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

            QFile f(savePath);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::critical(this, "Download Binary",
                    "Could not write to:\n" + savePath + "\n\n" + f.errorString());
                return;
            }
            f.write(reinterpret_cast<const char*>(chosen->bytes.data()),
                    static_cast<qint64>(chosen->bytes.size()));
            f.close();

            addActivityEntry(QString("Download complete: %1 (%2 bytes) → %3")
                .arg(QString::fromStdString(chosen->filename))
                .arg(chosen->bytes.size())
                .arg(savePath));
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
            m_projectTree->clear();
            m_projectSection->hide();
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
    // Invalidate any in-flight expandFolder() callbacks so they don't write
    // into nodes that belong to this freshly-rebuilt tree.
    ++m_treeGeneration;
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
    // Snapshot the generation at dispatch time.  The callback will no-op if
    // populateRepos() has been called in the meantime (which deletes `parent`).
    int gen = m_treeGeneration;

    // Open repo if needed (idempotent).
    BinaryNinja::WorkerEnqueue([this, parent, repo, folder, gen]() {
        auto& conn = GhidraConnection::instance();
        std::string err;
        conn.openRepo(repo, err);

        auto subfolders = conn.getSubfolders(repo, folder, err);
        auto items      = conn.listItems(repo, folder, err);

        QMetaObject::invokeMethod(this, [this, parent, repo, folder,
                                         subfolders, items, gen,
                                         errStr = QString::fromStdString(err)]() {
            // If the tree was rebuilt while we were fetching, our `parent`
            // pointer is dangling — bail out before touching any tree nodes.
            if (gen != m_treeGeneration) return;
            if (!errStr.isEmpty()) { logError(errStr); return; }

            if (subfolders.empty() && items.empty()) {
                addActivityEntry(QString(
                    "(folder '%1' in '%2' is empty — make sure the file is "
                    "checked in / added to version control on the Ghidra server, "
                    "not just imported into a local project)")
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

void ProjectPanel::addItemToProject(const QString& repo,
                                     const QString& folder,
                                     const QString& name)
{
    auto project = currentOpenProject(m_currentView);
    if (!project) {
        // No project open — fall back to the standalone open/download dialog.
        addActivityEntry("No BN project is open — use File → New Project to create one first.");
        openItemIntoNewView(repo, folder, name);
        return;
    }

    std::string repoStr   = repo.toStdString();
    std::string folderStr = folder.toStdString();
    std::string nameStr   = name.toStdString();

    // If this Ghidra item is already in the project, just open the existing file.
    for (const auto& pf : project->GetFiles()) {
        auto lm = project->QueryMetadata("ghidra.link." + pf->GetId());
        if (!lm || !lm->IsKeyValueStore()) continue;
        auto kv = lm->GetKeyValueStore();
        auto get = [&](const std::string& k) {
            auto it = kv.find(k);
            return (it != kv.end() && it->second->IsString()) ? it->second->GetString() : "";
        };
        if (get("repo") == repoStr && get("folder") == folderStr && get("item") == nameStr) {
            addActivityEntry(QString("'%1' is already in the project — opening.").arg(name));
            if (auto* ctx = UIContext::activeContext())
                ctx->openFilename(QString::fromStdString(pf->GetPathOnDisk()));
            return;
        }
    }

    addActivityEntry(QString("Downloading '%1' from Ghidra and adding to project…").arg(name));

    // Capture connection info on the main thread before the worker runs.
    std::string host = GhidraConnection::instance().connectedHost();
    uint64_t    port = static_cast<uint64_t>(GhidraConnection::instance().connectedPort());
    std::string user = GhidraConnection::instance().connectedUser();

    BinaryNinja::WorkerEnqueue(
        [this, project, repoStr, folderStr, nameStr, name, host, port, user]() {

        std::string err;
        auto files = GhidraConnection::instance().downloadBinary(
            repoStr, folderStr, nameStr, -1, err);

        if (!err.empty() || files.empty()) {
            QString msg = !err.empty() ? QString::fromStdString(err)
                                       : "No binary data found in the Ghidra database.";
            QMetaObject::invokeMethod(this, [this, msg]() { logError(msg); },
                                      Qt::QueuedConnection);
            return;
        }

        // Prefer a source file whose name matches the item; otherwise use the first.
        const GhidraConnection::BinaryFile* chosen = &files[0];
        for (const auto& f : files)
            if (QString::fromStdString(f.filename).compare(name, Qt::CaseInsensitive) == 0)
                { chosen = &f; break; }

        if (files.size() > 1)
            BinaryNinja::LogInfo("Ghidra: multiple source files — using '%s'",
                                 chosen->filename.c_str());

        // Write bytes to a temp file (CreateFileFromPath copies it into the project dir).
        QString tmpPath = QDir::temp().filePath(name);
        {
            QFile f(tmpPath);
            if (!f.open(QIODevice::WriteOnly)) {
                QString e = f.errorString();
                QMetaObject::invokeMethod(this, [this, e]() {
                    logError("Failed to write temp file: " + e);
                }, Qt::QueuedConnection);
                return;
            }
            f.write(reinterpret_cast<const char*>(chosen->bytes.data()),
                    static_cast<qint64>(chosen->bytes.size()));
        } // file closed here

        auto projectFile = project->CreateFileFromPath(
            tmpPath.toStdString(), /*folder=*/nullptr, nameStr,
            "Ghidra: " + repoStr + "/" + folderStr + "/" + nameStr);

        QFile::remove(tmpPath);

        if (!projectFile) {
            QMetaObject::invokeMethod(this, [this]() {
                logError("Failed to add binary to BN project.");
            }, Qt::QueuedConnection);
            return;
        }

        std::string fileId   = projectFile->GetId();
        std::string filePath = projectFile->GetPathOnDisk();

        // Back on the main thread: store metadata BEFORE calling openFilename so
        // that notifyViewChanged finds the link and can show the linked status.
        QMetaObject::invokeMethod(this, [this, project, fileId, filePath,
                                          repoStr, folderStr, nameStr,
                                          host, port, user, name]() {
            if (!project->QueryMetadata("ghidra.server")) {
                std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                md["host"] = new BinaryNinja::Metadata(host);
                md["port"] = new BinaryNinja::Metadata(port);
                md["user"] = new BinaryNinja::Metadata(user);
                project->StoreMetadata("ghidra.server", new BinaryNinja::Metadata(md));
            }
            {
                std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                md["repo"]   = new BinaryNinja::Metadata(repoStr);
                md["folder"] = new BinaryNinja::Metadata(folderStr);
                md["item"]   = new BinaryNinja::Metadata(nameStr);
                project->StoreMetadata("ghidra.link." + fileId,
                                       new BinaryNinja::Metadata(md));
            }

            addActivityEntry(
                QString("Added '%1' to project — opening and importing symbols…").arg(name));
            scheduleRefreshProjectFiles();

            if (auto* ctx = UIContext::activeContext())
                ctx->openFilename(QString::fromStdString(filePath));
            // notifyViewChanged fires when BN opens the view and reads the link metadata.
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

    // If a project is open and a file is already linked to this repo item,
    // open that local file directly instead of re-importing.
    if (auto project = currentOpenProject(m_currentView)) {
        std::string repoS   = repo.toStdString();
        std::string folderS = folder.toStdString();
        std::string nameS   = name.toStdString();

        for (const auto& pf : project->GetFiles()) {
            auto lm = project->QueryMetadata("ghidra.link." + pf->GetId());
            if (!lm || !lm->IsKeyValueStore()) continue;
            auto kv = lm->GetKeyValueStore();
            auto get = [&](const std::string& k) {
                auto it = kv.find(k);
                return (it != kv.end() && it->second->IsString())
                           ? it->second->GetString() : "";
            };
            if (get("repo") == repoS && get("folder") == folderS && get("item") == nameS) {
                // Found the linked project file — open it.
                if (auto* ctx = UIContext::activeContext())
                    ctx->openProjectFile(pf);
                return;
            }
        }

        // No existing link — download + add to project + open + auto-import.
        addItemToProject(repo, folder, name);
        return;
    }

    auto& conn = GhidraConnection::instance();

    // If the current view is already checked out as this exact item, re-import
    // to pull in any server-side changes (inline sync).
    if (m_currentView &&
        conn.isCheckinItem(repo.toStdString(), folder.toStdString(), name.toStdString())) {
        addActivityEntry(QString("Re-syncing '%1' from Ghidra…").arg(name));
        importItem(repo.toStdString(), folder.toStdString(), name.toStdString());
        return;
    }

    // No project open — legacy behaviour.
    if (!m_currentView) {
        openItemIntoNewView(repo, folder, name);
        return;
    }

    importItem(repo.toStdString(), folder.toStdString(), name.toStdString());
}

void ProjectPanel::openItemIntoNewView(const QString& repo,
                                        const QString& folder,
                                        const QString& name)
{
    // Offer the user a choice: open a local file they already have, or
    // download the binary from the Ghidra server and open that.
    QDialog dlg(this);
    dlg.setWindowTitle("Open — " + name);
    dlg.setFixedWidth(400);

    auto* label = new QLabel(
        QString("No binary view is open. How would you like to open <b>%1</b>?").arg(name),
        &dlg);
    label->setWordWrap(true);

    auto* openBtn     = new QPushButton("Open existing file…",     &dlg);
    auto* downloadBtn = new QPushButton("Download from Ghidra…",   &dlg);
    auto* cancelBtn   = new QPushButton("Cancel",                  &dlg);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(label);
    layout->addSpacing(8);
    layout->addWidget(openBtn);
    layout->addWidget(downloadBtn);
    layout->addWidget(cancelBtn);

    int choice = 0; // 1 = open existing, 2 = download
    connect(openBtn,     &QPushButton::clicked, [&]{ choice = 1; dlg.accept(); });
    connect(downloadBtn, &QPushButton::clicked, [&]{ choice = 2; dlg.accept(); });
    connect(cancelBtn,   &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    if (choice == 1) {
        // Let the user browse for an existing .bndb or raw binary.
        QString path = QFileDialog::getOpenFileName(
            this, "Open File — " + name, QString(),
            "Binary Ninja Database (*.bndb);;All Files (*)");
        if (path.isEmpty()) return;
        auto* ctx = UIContext::activeContext();
        if (ctx) ctx->openFilename(path);

    } else if (choice == 2) {
        // Download the binary from the Ghidra server and open it.
        QString savePath = QFileDialog::getSaveFileName(
            this, "Download Binary — " + name, name, "All Files (*)");
        if (savePath.isEmpty()) return;

        std::string repoStr   = repo.toStdString();
        std::string folderStr = folder.toStdString();
        std::string nameStr   = name.toStdString();

        BinaryNinja::WorkerEnqueue([this, repoStr, folderStr, nameStr, savePath]() {
            std::string err;
            auto files = GhidraConnection::instance().downloadBinary(
                repoStr, folderStr, nameStr, -1, err);

            QMetaObject::invokeMethod(this, [this, files = std::move(files),
                                              errStr = QString::fromStdString(err),
                                              savePath]() mutable {
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

                // If multiple source files, let the user pick one.
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
                        "This database contains multiple source files. Select one:",
                        labels, 0, false, &ok);
                    if (!ok) return;
                    int idx = labels.indexOf(sel);
                    if (idx >= 0 && idx < (int)files.size())
                        chosen = &files[idx];
                }

                QFile f(savePath);
                if (!f.open(QIODevice::WriteOnly)) {
                    logError("Could not write to: " + savePath + " — " + f.errorString());
                    return;
                }
                f.write(reinterpret_cast<const char*>(chosen->bytes.data()),
                        static_cast<qint64>(chosen->bytes.size()));
                f.close();

                addActivityEntry(QString("Downloaded %1 (%2 bytes) → %3")
                    .arg(QString::fromStdString(chosen->filename))
                    .arg(chosen->bytes.size())
                    .arg(savePath));

                auto* ctx = UIContext::activeContext();
                if (ctx) ctx->openFilename(savePath);
            }, Qt::QueuedConnection);
        });
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ProjectPanel::onEventReceived(GhidraEvent evt) {
    // REP_OPEN_HANDLE_COUNT is a server-internal bookkeeping signal (connection
    // reference-counting) that has no meaning for the user — suppress it.
    if (evt.type == "REP_OPEN_HANDLE_COUNT") {
        // Still fall through so project-file refresh logic below can run if needed.
    } else {
        QString msg = QString("[%1] %2 → %3/%4")
            .arg(QString::fromStdString(evt.repo))
            .arg(QString::fromStdString(evt.type))
            .arg(QString::fromStdString(evt.parentPath))
            .arg(QString::fromStdString(evt.name));
        addActivityEntry(msg);
    }

    // Check whether this change touches any file in the open BN project.
    // If so, notify the user so they know to sync.
    if (evt.name.empty()) return;
    auto project = currentOpenProject(m_currentView);
    if (!project) return;

    for (const auto& pf : project->GetFiles()) {
        auto lm = project->QueryMetadata("ghidra.link." + pf->GetId());
        if (!lm || !lm->IsKeyValueStore()) continue;
        auto kv = lm->GetKeyValueStore();
        auto get = [&](const std::string& k) {
            auto it = kv.find(k);
            return (it != kv.end() && it->second->IsString()) ? it->second->GetString() : "";
        };
        if (get("repo") == evt.repo && get("item") == evt.name) {
            addActivityEntry(
                QString("↻ '%1' was updated on the server — "
                        "double-click it in the repository tree to sync symbols.")
                .arg(QString::fromStdString(pf->GetName())));
            // Colour shift in the project panel will reflect "not active checkout".
            scheduleRefreshProjectFiles();
            break;
        }
    }
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
        auto& conn = GhidraConnection::instance();
        conn.storeCheckinState(
            std::move(result.addrToKey),
            std::move(result.addrToOriginalName),
            std::move(result.addrToCommentKey),
            std::move(result.addrToOriginalComment),
            std::move(result.addrToOriginalFuncComment),
            std::move(result.addrToCommentField),
            std::move(result.ghidraDataTypes),
            std::move(result.parameters),
            std::move(result.bookmarks),
            std::move(result.dataItems),
            std::move(result.equates),
            std::move(result.funcSigs),
            result.imageBase,
            repo, folder, name);

        // Persist the Ghidra link so it can be restored on reopen.
        //
        // If this file belongs to a BN Project we use two project-level keys:
        //   "ghidra.server"          → { host, port, user }   (shared by all files)
        //   "ghidra.link.<fileId>"   → { repo, folder, item } (per-file)
        //
        // We also always write "ghidra.link" into the .bndb itself so that
        // standalone files (opened outside of a project) still work.
        auto   project = projectForView(view);
        auto   fileId  = projectFileId(view);

        if (project && !fileId.empty()) {
            {
                std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                md["host"] = new BinaryNinja::Metadata(conn.connectedHost());
                md["port"] = new BinaryNinja::Metadata(static_cast<uint64_t>(conn.connectedPort()));
                md["user"] = new BinaryNinja::Metadata(conn.connectedUser());
                project->StoreMetadata("ghidra.server", new BinaryNinja::Metadata(md));
            }
            {
                std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                md["repo"]        = new BinaryNinja::Metadata(repo);
                md["folder"]      = new BinaryNinja::Metadata(folder);
                md["item"]        = new BinaryNinja::Metadata(name);
                md["checked_out"] = new BinaryNinja::Metadata(static_cast<uint64_t>(1));
                project->StoreMetadata("ghidra.link." + fileId, new BinaryNinja::Metadata(md));
            }
        }

        if (view) {
            std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
            md["host"]   = new BinaryNinja::Metadata(conn.connectedHost());
            md["port"]   = new BinaryNinja::Metadata(static_cast<uint64_t>(conn.connectedPort()));
            md["user"]   = new BinaryNinja::Metadata(conn.connectedUser());
            md["repo"]   = new BinaryNinja::Metadata(repo);
            md["folder"] = new BinaryNinja::Metadata(folder);
            md["item"]   = new BinaryNinja::Metadata(name);
            view->StoreMetadata("ghidra.link", new BinaryNinja::Metadata(md), /*isAuto=*/true);
        }

        QMetaObject::invokeMethod(this, [this,
                                          syms    = result.symbolsApplied,
                                          comms   = result.commentsApplied,
                                          flags   = result.flagsApplied,
                                          funcsCreated = result.functionsCreated,
                                          sectionsAdded = result.sectionsAdded,
                                          segmentsAdded = result.segmentsAdded,
                                          addrMin = result.addrMin,
                                          addrMax = result.addrMax,
                                          sample  = result.sampleSymbol,
                                          bnBase,
                                          itemName = QString::fromStdString(name),
                                          qRepo    = QString::fromStdString(repo),
                                          qFolder  = QString::fromStdString(folder),
                                          qName    = QString::fromStdString(name),
                                          qHost    = QString::fromStdString(conn.connectedHost()),
                                          qUser    = QString::fromStdString(conn.connectedUser()),
                                          connPort = conn.connectedPort()]() {
            addActivityEntry(QString("Import complete: %1 — %2 symbols, %3 comments, %4 flags")
                .arg(itemName).arg(syms).arg(comms).arg(flags));
            if (funcsCreated > 0) {
                addActivityEntry(QString("  Created %1 function(s) Ghidra found that BN had not")
                    .arg(funcsCreated));
            }
            if (sectionsAdded > 0 || segmentsAdded > 0) {
                addActivityEntry(QString("  Memory map: %1 section(s), %2 new segment(s)")
                    .arg(sectionsAdded).arg(segmentsAdded));
            }
            if (syms > 0) {
                addActivityEntry(QString("  Ghidra addr range: 0x%1 – 0x%2  (sample: %3)")
                    .arg(addrMin, 0, 16).arg(addrMax, 0, 16)
                    .arg(QString::fromStdString(sample)));
                addActivityEntry(QString("  BN view start: 0x%1").arg(bnBase, 0, 16));
            }
            // Mark the item green — it is now our active checkout.
            if (auto* tItem = findTreeItem(qRepo, qFolder, qName))
                setItemCheckedOut(tItem, true);

            // Populate m_linkedGhidra so "Check In…" appears immediately and the
            // linkedHere check in the context menu works in the same session.
            m_linkedGhidra.host   = qHost;
            m_linkedGhidra.port   = connPort;
            m_linkedGhidra.user   = qUser;
            m_linkedGhidra.repo   = qRepo;
            m_linkedGhidra.folder = qFolder;
            m_linkedGhidra.item   = qName;

            // Persist checkout state so "Check In…" survives a BN restart:
            // sets m_checkedOut = true and writes checked_out:1 into project metadata.
            persistCheckedOutState(true);

            // Refresh the project panel so the status column reflects the new link.
            scheduleRefreshProjectFiles();
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

// ---------------------------------------------------------------------------
// Debounced repo-tree status refresh
// ---------------------------------------------------------------------------

void ProjectPanel::scheduleRefreshStatus() {
    // Lazily create the timer — parent is this, so it is destroyed with the widget.
    if (!m_statusTimer) {
        m_statusTimer = new QTimer(this);
        m_statusTimer->setSingleShot(true);
        m_statusTimer->setInterval(200); // ms — coalesces bursts from notifyViewChanged
        connect(m_statusTimer, &QTimer::timeout,
                this, &ProjectPanel::refreshStatus);
    }
    // (Re-)starting resets the countdown, so multiple rapid calls stay coalesced.
    m_statusTimer->start();
}

// ---------------------------------------------------------------------------
// Project files panel
// ---------------------------------------------------------------------------

void ProjectPanel::scheduleRefreshProjectFiles() {
    // Lazily create the timer — parent is this, so it is destroyed with the widget.
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(150); // ms — coalesces bursts during importItem
        connect(m_refreshTimer, &QTimer::timeout,
                this, &ProjectPanel::refreshProjectFiles);
    }
    // (Re-)starting resets the countdown, so multiple rapid calls stay coalesced.
    m_refreshTimer->start();
}

void ProjectPanel::refreshProjectFiles() {
    auto project = projectForView(m_currentView);
    if (!project) {
        m_projectSection->hide();
        return;
    }

    auto& conn  = GhidraConnection::instance();
    auto  files = project->GetFiles();

    m_projectTree->clear();

    for (const auto& pf : files) {
        auto* row = new QTreeWidgetItem(m_projectTree);
        row->setText(0, QString::fromStdString(pf->GetName()));
        // Store the project file ID for double-click → openProjectFile, and the
        // on-disk path as a fallback for context-menu file-path matching.
        row->setData(0, Qt::UserRole,      QString::fromStdString(pf->GetId()));
        row->setData(0, Qt::UserRole + 10, QString::fromStdString(pf->GetPathOnDisk()));

        auto linkMeta = project->QueryMetadata("ghidra.link." + pf->GetId());
        if (linkMeta && linkMeta->IsKeyValueStore()) {
            auto kv = linkMeta->GetKeyValueStore();
            auto get = [&](const std::string& k) {
                auto it = kv.find(k);
                return (it != kv.end() && it->second->IsString())
                    ? it->second->GetString() : "";
            };

            std::string repo   = get("repo");
            std::string folder = get("folder");
            std::string name   = get("item");

            // "folder/item" display — omit leading "/" for cleanliness.
            QString display = QString::fromStdString(name);
            if (folder != "/" && !folder.empty())
                display = QString::fromStdString(folder) + display;

            bool isActive = conn.isCheckinItem(repo, folder, name);
            QColor col = isActive ? QColor(0x44, 0xee, 0x66) : QColor(0x44, 0xaa, 0xff);
            row->setText(1, display);
            row->setForeground(0, col);
            row->setForeground(1, col);
            row->setToolTip(1, QString("Repo: %1\nFolder: %2\nItem: %3")
                .arg(QString::fromStdString(repo))
                .arg(QString::fromStdString(folder))
                .arg(QString::fromStdString(name)));

            // Store coords so a future context menu can trigger import.
            row->setData(0, Qt::UserRole + 1, QString::fromStdString(repo));
            row->setData(0, Qt::UserRole + 2, QString::fromStdString(folder));
            row->setData(0, Qt::UserRole + 3, QString::fromStdString(name));
        } else {
            row->setText(1, "—");
            row->setForeground(0, QColor(0x77, 0x77, 0x77));
            row->setForeground(1, QColor(0x77, 0x77, 0x77));
            row->setToolTip(1, "Not linked to a Ghidra item");
        }
    }

    m_projectSection->setVisible(!files.empty());
}

void ProjectPanel::onProjectItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    // UserRole holds the project file ID; look it up so BN uses its display name
    // (e.g. "ls") for the tab rather than the internal GUID-based on-disk path.
    QString fileId = item->data(0, Qt::UserRole).toString();
    auto* ctx = UIContext::activeContext();
    if (!ctx) return;

    if (!fileId.isEmpty()) {
        auto project = projectForView(m_currentView);
        if (project) {
            for (const auto& pf : project->GetFiles()) {
                if (QString::fromStdString(pf->GetId()) == fileId) {
                    ctx->openProjectFile(pf);
                    return;
                }
            }
        }
    }
    // Fallback: open by disk path (standalone file, or project lookup failed).
    QString path = item->data(0, Qt::UserRole + 10).toString();
    if (!path.isEmpty())
        ctx->openFilename(path);
}

void ProjectPanel::migrateStandaloneMetadata() {
    // Only relevant when the file is in a project but was imported before the
    // project workflow existed (metadata lives only in the .bndb, not in the
    // project).  Copy it up so the Project Files panel shows the link.
    if (!m_currentView) return;

    auto   project = projectForView(m_currentView);
    auto   fileId  = projectFileId(m_currentView);
    if (!project || fileId.empty()) return;

    // Nothing to do if the project entry already exists.
    if (project->QueryMetadata("ghidra.link." + fileId)) return;

    auto bndbMeta = m_currentView->QueryMetadata("ghidra.link");
    if (!bndbMeta || !bndbMeta->IsKeyValueStore()) return;

    auto kv = bndbMeta->GetKeyValueStore();
    auto getString = [&](const std::string& k) {
        auto it = kv.find(k);
        return (it != kv.end() && it->second->IsString()) ? it->second->GetString() : "";
    };
    auto getUInt = [&](const std::string& k) -> uint64_t {
        auto it = kv.find(k);
        return (it != kv.end() && it->second->IsUnsignedInteger())
            ? it->second->GetUnsignedInteger() : 0;
    };

    // Write server info only if the project doesn't have it yet.
    if (!project->QueryMetadata("ghidra.server")) {
        std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
        md["host"] = new BinaryNinja::Metadata(getString("host"));
        md["port"] = new BinaryNinja::Metadata(getUInt("port"));
        md["user"] = new BinaryNinja::Metadata(getString("user"));
        project->StoreMetadata("ghidra.server", new BinaryNinja::Metadata(md));
    }

    // Write per-file item info.
    {
        std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
        md["repo"]   = new BinaryNinja::Metadata(getString("repo"));
        md["folder"] = new BinaryNinja::Metadata(getString("folder"));
        md["item"]   = new BinaryNinja::Metadata(getString("item"));
        project->StoreMetadata("ghidra.link." + fileId, new BinaryNinja::Metadata(md));
    }

    addActivityEntry("Migrated Ghidra link from .bndb into project metadata.");
}

// ---------------------------------------------------------------------------
// Project files context menu + upload
// ---------------------------------------------------------------------------

void ProjectPanel::onProjectContextMenu(const QPoint& pos) {
    auto* item = m_projectTree->itemAt(pos);
    if (!item) return;

    // UserRole   = project file ID (used by double-click → openProjectFile)
    // UserRole+10 = on-disk path   (used here for context-menu operations)
    QString pfId     = item->data(0, Qt::UserRole).toString();
    QString filePath = item->data(0, Qt::UserRole + 10).toString();
    QString fileName = item->text(0);
    // UserRole+1 is repo (empty string if no Ghidra link).
    bool hasLink = !item->data(0, Qt::UserRole + 1).toString().isEmpty();

    auto& conn = GhidraConnection::instance();

    QMenu menu;

    if (!hasLink && conn.isServerConnected()) {
        // File is in the BN project but not yet on the Ghidra server.
        menu.addAction("Upload to Ghidra Server…",
            [this, filePath, fileName, pfId]() {
                uploadToGhidra(filePath, fileName, pfId);
            });
    } else if (hasLink) {
        // File already has a Ghidra link — offer a quick re-import.
        QString repo   = item->data(0, Qt::UserRole + 1).toString();
        QString folder = item->data(0, Qt::UserRole + 2).toString();
        QString name   = item->data(0, Qt::UserRole + 3).toString();
        menu.addAction("Sync from Ghidra (re-import symbols)",
            [this, repo, folder, name]() {
                importItem(repo.toStdString(), folder.toStdString(), name.toStdString());
            });

        menu.addSeparator();

        menu.addAction("Clear Ghidra Link…",
            [this, filePath, pfId, name]() {
                auto btn = QMessageBox::question(
                    this, "Clear Ghidra Link",
                    QString("Remove the Ghidra server link for '%1'?\n\n"
                            "This clears the stored repo/item reference so you can "
                            "re-upload or re-link the file. It does not delete anything "
                            "from the Ghidra server.").arg(name),
                    QMessageBox::Yes | QMessageBox::Cancel);
                if (btn != QMessageBox::Yes) return;

                // 1. Remove from BN project metadata (the authoritative store).
                if (auto project = currentOpenProject(m_currentView)) {
                    if (!pfId.isEmpty())
                        project->RemoveMetadata("ghidra.link." + pfId.toStdString());
                }

                // 2. If this is the currently open file, also clear its .bndb metadata
                //    and the in-memory link so notifyViewChanged stops auto-retrying.
                if (m_currentView) {
                    QString viewFile = QString::fromStdString(
                        m_currentView->GetFile()->GetFilename());
                    if (viewFile == filePath || viewFile.endsWith("/" + QFileInfo(filePath).fileName())) {
                        m_currentView->RemoveMetadata("ghidra.link");
                        m_linkedGhidra = {};
                    }
                }

                addActivityEntry(QString("Ghidra link cleared for '%1'.").arg(name));
                scheduleRefreshProjectFiles();
            });
    } else if (!conn.isServerConnected()) {
        auto* act = menu.addAction("Upload to Ghidra Server…");
        act->setEnabled(false);
        act->setToolTip("Connect to a Ghidra server first");
    }

    if (!menu.actions().isEmpty())
        menu.exec(m_projectTree->viewport()->mapToGlobal(pos));
}

void ProjectPanel::uploadToGhidra(const QString& filePath,
                                   const QString& fileName,
                                   const QString& projectFileId)
{
    auto& conn = GhidraConnection::instance();
    if (!conn.isServerConnected()) {
        logError("Not connected to Ghidra server — click Connect first.");
        return;
    }

    // ---- Populate repo list from the already-loaded repository tree ----------
    QStringList repos;
    for (int i = 0; i < m_repoTree->topLevelItemCount(); ++i)
        repos << m_repoTree->topLevelItem(i)->data(0, Qt::UserRole + 1).toString();

    // ---- Dialog ---------------------------------------------------------------
    QDialog dlg(this);
    dlg.setWindowTitle("Upload to Ghidra Server — " + fileName);
    dlg.setMinimumWidth(440);

    auto* repoCombo   = new QComboBox(&dlg);
    for (const auto& r : repos) if (!r.isEmpty()) repoCombo->addItem(r);
    if (repoCombo->count() == 0) repoCombo->addItem(""); // fallback editable field
    repoCombo->setEditable(repos.isEmpty());

    auto* folderEdit  = new QLineEdit("/", &dlg);
    folderEdit->setPlaceholderText("/");
    // Strip .bndb extension — Ghidra item names don't carry BN-specific extensions.
    QString defaultItemName = fileName;
    if (defaultItemName.endsWith(".bndb", Qt::CaseInsensitive))
        defaultItemName.chop(5);
    auto* nameEdit    = new QLineEdit(defaultItemName, &dlg);
    auto* commentEdit = new QLineEdit("Uploaded from Binary Ninja", &dlg);
    auto* keepCo      = new QCheckBox("Keep checked out after upload", &dlg);
    keepCo->setToolTip(
        "Reserves an exclusive checkout so you can push symbol renames and\n"
        "comments back to Ghidra via Check In… after importing symbols.");

    auto* form = new QFormLayout;
    form->addRow("Repository:", repoCombo);
    form->addRow("Folder:",     folderEdit);
    form->addRow("Item name:",  nameEdit);
    form->addRow("Comment:",    commentEdit);
    form->addRow("",            keepCo);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Upload");
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QString repo    = repoCombo->currentText().trimmed();
    QString folder  = folderEdit->text().trimmed();
    QString item    = nameEdit->text().trimmed();
    QString comment = commentEdit->text().trimmed();
    bool    keep    = keepCo->isChecked();

    if (repo.isEmpty() || item.isEmpty()) {
        logError("Repository and item name are required.");
        return;
    }
    // Normalise folder — must start with "/"
    if (!folder.startsWith("/")) folder.prepend("/");

    // ---- Get the raw binary bytes -----------------------------------------------
    // The filePath points to a .bndb (BN's database format), which analyzeHeadless
    // cannot import.  Instead we read the raw binary from the underlying BN view:
    // walk the parent-view chain to the innermost "Raw" view, which holds the
    // original ELF/PE/Mach-O bytes exactly as they were when BN first opened them.
    //
    // If the current view doesn't correspond to this project file (or no view is
    // open) we fall back to a file picker so the user can point us at the binary.
    std::vector<uint8_t> bytes;
    {
        BinaryViewRef rawView;
        const std::string pfIdStr = projectFileId.toStdString();

        // Search all open UIContexts/views for one that belongs to this project file.
        for (auto* ctx : UIContext::allContexts()) {
            for (auto& [view, viewName] : ctx->getAvailableBinaryViews()) {
                if (!view) continue;
                auto pf = view->GetFile()->GetProjectFile();
                if (!pf || pf->GetId() != pfIdStr) continue;
                // Walk to the innermost Raw view that holds the original file bytes.
                rawView = view;
                while (rawView->GetParentView())
                    rawView = rawView->GetParentView();
                break;
            }
            if (rawView) break;
        }

        if (rawView && rawView->GetLength() > 0) {
            auto buf = rawView->ReadBuffer(rawView->GetStart(), rawView->GetLength());
            const uint8_t* data = static_cast<const uint8_t*>(buf.GetData());
            bytes.assign(data, data + buf.GetLength());
            addActivityEntry(
                QString("Reading binary from open view (%1 bytes)…").arg(bytes.size()));
        } else {
            // File is not currently open — prompt the user to open it first, or
            // fall back to a manual file picker if they prefer.
            auto btn = QMessageBox::question(
                this, "Upload to Ghidra",
                QString("'%1' is not currently open in Binary Ninja.\n\n"
                        "Open it in BN first and retry Upload, or choose "
                        "\"Select File\" to locate the original binary manually.")
                    .arg(item),
                QMessageBox::Open | QMessageBox::Cancel,
                QMessageBox::Open);

            if (btn == QMessageBox::Open) {
                // Try to open via the project so BN registers it correctly.
                if (auto* ctx = UIContext::activeContext()) {
                    auto project = currentOpenProject(m_currentView);
                    if (project) {
                        for (const auto& pf : project->GetFiles()) {
                            if (pf->GetId() == pfIdStr) {
                                ctx->openProjectFile(pf);
                                break;
                            }
                        }
                    }
                }
                addActivityEntry(
                    QString("Opening '%1' — retry Upload after it finishes loading.").arg(item));
                return;
            }
            // Cancel
            return;
        }
    }

    if (bytes.empty()) {
        logError("Could not obtain binary bytes for upload.");
        return;
    }

    addActivityEntry(
        QString("Uploading '%1' to Ghidra repo '%2'%3…")
        .arg(item).arg(repo)
        .arg(folder == "/" ? "" : " folder " + folder));

    std::string repoStr    = repo.toStdString();
    std::string folderStr  = folder.toStdString();
    std::string itemStr    = item.toStdString();
    std::string commentStr = comment.toStdString();
    std::string pfId       = projectFileId.toStdString();
    auto        project    = currentOpenProject(m_currentView);

    // ---- Collect BN analysis from the current view ---------------------------
    // We snapshot functions (with names and plate comments) and address comments
    // now, on the UI thread, so the worker gets a stable copy.  If m_currentView
    // isn't set (e.g. the user opened the dialog without an active file) we skip
    // the sync and let Ghidra store the raw binary without annotations.
    // Tab-delimited data file consumed by BinjaSyncScript.java (the analyzeHeadless postScript).
    // Format:
    //   F\t<hexaddr>\t<name>\t<plate>   — function (name/plate may be empty)
    //   C\t<hexaddr>\t<text>            — EOL comment
    // Backslashes, tabs and newlines inside text fields are escaped as \\, \t, \n.
    std::string analysisJson;
    if (m_currentView) {
        // Escape backslashes first, then tabs and newlines so the Java side can safely split on \t.
        auto escape = [](const QString& s) -> QString {
            return QString(s)
                .replace("\\", "\\\\")
                .replace("\t",  "\\t")
                .replace("\n",  "\\n");
        };

        QString lines;

        for (auto& func : m_currentView->GetAnalysisFunctionList()) {
            if (!func) continue;
            uint64_t start = func->GetStart();
            QString  name  = QString::fromStdString(func->GetSymbol()->GetRawName());
            QString  plate = QString::fromStdString(func->GetComment());

            // Determine whether the BN name is auto-generated (prefix + hex address).
            // Auto-named functions are sent with an EMPTY name so BinjaSyncScript
            // still creates the function entry in Ghidra (giving it a DB key and a
            // FUN_XXXXXXXX auto-name) without stomping it with a BN auto-label.
            // User-defined names are sent as-is so they land in Ghidra immediately.
            //
            // We emit EVERY function (not just user-named ones) so that every BN
            // entry-point gets a symbol key in the Ghidra DB.  Without this,
            // collectCheckinChanges() has no m_addrToKey entry for functions that
            // were auto-named at upload time and later renamed in BN — making
            // those renames invisible to the check-in flow.
            bool isDefault = name.startsWith("sub_") || name.startsWith("FUN_") ||
                             name.startsWith("j_")   || name.isEmpty();

            lines += QString("F\t0x%1\t%2\t%3\n")
                .arg(start, 0, 16)
                .arg(escape(isDefault ? QString() : name))
                .arg(escape(plate));
        }

        for (uint64_t addr : m_currentView->GetCommentedAddresses()) {
            QString text = QString::fromStdString(
                m_currentView->GetCommentForAddress(addr));
            if (text.isEmpty()) continue;
            lines += QString("C\t0x%1\t%2\n")
                .arg(addr, 0, 16)
                .arg(escape(text));
        }

        if (!lines.isEmpty())
            analysisJson = lines.toStdString();
    }

    BinaryNinja::WorkerEnqueue(
        [this, repoStr, folderStr, itemStr, commentStr, keep, analysisJson,
         bytes = std::move(bytes), project, pfId, item, repo, folder]() mutable {

        std::string err;
        bool ok = GhidraConnection::instance().uploadBinary(
            repoStr, folderStr, itemStr, bytes, commentStr, keep, analysisJson, err);

        QMetaObject::invokeMethod(this,
            [this, ok, err = QString::fromStdString(err),
             repoStr, folderStr, itemStr, keep, project, pfId, item, repo, folder]() {

            if (!ok) {
                logError("Upload failed: " + err);
                return;
            }

            addActivityEntry(
                QString("✓ '%1' uploaded to Ghidra server successfully.%2")
                .arg(item)
                .arg(keep ? " It is now checked out — use Check In… to push BN changes."
                          : ""));

            // ---- Store Ghidra link in project metadata ----------------------
            auto& c = GhidraConnection::instance();
            if (project && !pfId.empty()) {
                if (!project->QueryMetadata("ghidra.server")) {
                    std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                    md["host"] = new BinaryNinja::Metadata(c.connectedHost());
                    md["port"] = new BinaryNinja::Metadata(
                        static_cast<uint64_t>(c.connectedPort()));
                    md["user"] = new BinaryNinja::Metadata(c.connectedUser());
                    project->StoreMetadata("ghidra.server",
                                           new BinaryNinja::Metadata(md));
                }
                {
                    std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md;
                    md["repo"]        = new BinaryNinja::Metadata(repoStr);
                    md["folder"]      = new BinaryNinja::Metadata(folderStr);
                    md["item"]        = new BinaryNinja::Metadata(itemStr);
                    md["checked_out"] = new BinaryNinja::Metadata(
                        static_cast<uint64_t>(keep ? 1 : 0));
                    project->StoreMetadata("ghidra.link." + pfId,
                                           new BinaryNinja::Metadata(md));
                }
            }

            // ---- Update m_linkedGhidra so "Check In…" appears immediately ----
            // notifyViewChanged normally sets this, but it won't fire just
            // because we wrote project metadata.  Populate it here so the user
            // can right-click the new item and check in without switching tabs.
            m_linkedGhidra.host   = QString::fromStdString(c.connectedHost());
            m_linkedGhidra.port   = c.connectedPort();
            m_linkedGhidra.user   = QString::fromStdString(c.connectedUser());
            m_linkedGhidra.repo   = repo;
            m_linkedGhidra.folder = folder;
            m_linkedGhidra.item   = item;
            // Track whether the item is now checked out so the context menu shows
            // "Check In…" (keep=true) vs "Check Out…" (keep=false) immediately.
            m_checkedOut = keep;

            // ---- Refresh the panel so the new link shows in green/blue -----
            scheduleRefreshProjectFiles();

            // ---- Expand / refresh the repo tree so the new item is visible --
            for (int i = 0; i < m_repoTree->topLevelItemCount(); ++i) {
                auto* node = m_repoTree->topLevelItem(i);
                if (node->data(0, Qt::UserRole + 1).toString() == repo) {
                    // Collapse then expand to re-fetch the folder contents.
                    node->setExpanded(false);
                    node->takeChildren();
                    new QTreeWidgetItem(node, QStringList{"Loading…"});
                    node->setExpanded(true);
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectPanel::notifyViewChanged(ViewFrame* frame) {
    m_currentView = frame ? frame->getCurrentBinaryView() : BinaryViewRef{};
    m_linkedGhidra = {};

    if (!m_currentView) return;

    // Typed-read helpers for a KV metadata map.
    using KV = std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>>;
    auto kvStr = [](const KV& kv, const std::string& k) -> std::string {
        auto it = kv.find(k);
        return (it != kv.end() && it->second->IsString()) ? it->second->GetString() : "";
    };
    auto kvInt = [](const KV& kv, const std::string& k) -> int {
        auto it = kv.find(k);
        return (it != kv.end() && it->second->IsUnsignedInteger())
            ? static_cast<int>(it->second->GetUnsignedInteger()) : 0;
    };

    // --- 1. Try BN Project metadata (project owns server info + per-file item info) ---
    auto   project = projectForView(m_currentView);
    auto   fileId  = projectFileId(m_currentView);

    if (project && !fileId.empty()) {
        auto serverMeta = project->QueryMetadata("ghidra.server");
        auto itemMeta   = project->QueryMetadata("ghidra.link." + fileId);

        if (serverMeta && serverMeta->IsKeyValueStore() &&
            itemMeta   && itemMeta->IsKeyValueStore()) {
            auto skv = serverMeta->GetKeyValueStore();
            auto ikv = itemMeta->GetKeyValueStore();

            m_linkedGhidra.host   = QString::fromStdString(kvStr(skv, "host"));
            m_linkedGhidra.port   = kvInt(skv, "port");
            m_linkedGhidra.user   = QString::fromStdString(kvStr(skv, "user"));
            m_linkedGhidra.repo   = QString::fromStdString(kvStr(ikv, "repo"));
            m_linkedGhidra.folder = QString::fromStdString(kvStr(ikv, "folder"));
            m_linkedGhidra.item   = QString::fromStdString(kvStr(ikv, "item"));
            m_checkedOut          = kvInt(ikv, "checked_out") != 0;
        }
    }

    // --- 2. Fall back to .bndb-level metadata (standalone file, or pre-project import) ---
    if (!m_linkedGhidra.valid()) {
        auto meta = m_currentView->QueryMetadata("ghidra.link");
        if (meta && meta->IsKeyValueStore()) {
            auto kv = meta->GetKeyValueStore();
            m_linkedGhidra.host   = QString::fromStdString(kvStr(kv, "host"));
            m_linkedGhidra.port   = kvInt(kv, "port");
            m_linkedGhidra.user   = QString::fromStdString(kvStr(kv, "user"));
            m_linkedGhidra.repo   = QString::fromStdString(kvStr(kv, "repo"));
            m_linkedGhidra.folder = QString::fromStdString(kvStr(kv, "folder"));
            m_linkedGhidra.item   = QString::fromStdString(kvStr(kv, "item"));

            // File is in a project but was imported before the project workflow
            // existed — promote .bndb metadata into the project now.
            migrateStandaloneMetadata();
        }
    }

    // Keep the project watcher pointed at the current project so that file
    // additions and deletions made through BN's own UI are reflected immediately.
    watchProject(currentOpenProject(m_currentView));

    // Rebuild the Project Files panel regardless of link validity.
    scheduleRefreshProjectFiles();

    if (!m_linkedGhidra.valid()) return;

    // The file is linked to a Ghidra server item.  Do NOT auto-import or
    // auto-checkout — let the user trigger that explicitly via "Check Out…"
    // in the repository tree when they are ready to pull symbols.
    scheduleRefreshStatus();
}

// ---------------------------------------------------------------------------
// persistCheckedOutState — update m_checkedOut and write the value into the
// project metadata ("ghidra.link.<fileId>") so it survives a BN restart.
// Must be called on the UI thread.
// ---------------------------------------------------------------------------
void ProjectPanel::persistCheckedOutState(bool val) {
    m_checkedOut = val;

    auto project = projectForView(m_currentView);
    auto fileId  = projectFileId(m_currentView);
    if (!project || fileId.empty()) return;

    auto itemMeta = project->QueryMetadata("ghidra.link." + fileId);
    if (!itemMeta || !itemMeta->IsKeyValueStore()) return;

    // Merge updated flag into the existing key-value store.
    auto ikv = itemMeta->GetKeyValueStore();
    std::map<std::string, BinaryNinja::Ref<BinaryNinja::Metadata>> md(ikv.begin(), ikv.end());
    md["checked_out"] = new BinaryNinja::Metadata(static_cast<uint64_t>(val ? 1 : 0));
    project->StoreMetadata("ghidra.link." + fileId, new BinaryNinja::Metadata(md));
}
