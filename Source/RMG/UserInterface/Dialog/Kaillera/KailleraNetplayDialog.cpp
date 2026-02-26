/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraNetplayDialog.hpp"
#include "KailleraServerBrowserDialog.hpp"
#include "KailleraP2PDialog.hpp"
#include "KailleraWaitingGamesDialog.hpp"

#ifdef _WIN32

#include "../../KailleraUIBridge.hpp"

#include <RMG-Core/Settings.hpp>
#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Emulation.hpp>

#include "n02_client.h"
#include "kailleraclient.h"
#include "kcore/kaillera_core.h"
#include "core/p2p_core.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QEventLoop>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QUrl>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QProcess>
#include <QSettings>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>

#include <cstring>
#include <ctime>
#include <fstream>
#include <future>

#include <windows.h>

KailleraNetplayDialog::KailleraNetplayDialog(QWidget* parent)
    : QDialog(parent)
{
    m_netManager = new QNetworkAccessManager(this);

    setupUI();
    // Start the KSSDFA state machine timer
    n02::setStateInput(0);
    m_stateMachineTimer = new QTimer(this);
    connect(m_stateMachineTimer, &QTimer::timeout, this, &KailleraNetplayDialog::onStateMachineTimer);
    m_stateMachineTimer->start(1);
    // Restore saved geometry
    std::string geom = CoreSettingsGetStringValue(SettingsID::Kaillera_NetplayGeometry);
    if (!geom.empty())
    {
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(geom)));
    }

}

KailleraNetplayDialog::~KailleraNetplayDialog()
{
    if (m_stateMachineTimer)
    {
        m_stateMachineTimer->stop();
    }
    saveSettings();
    saveServerList();

    // Save server table column widths
    if (m_serverTable)
    {
        QStringList widths;
        for (int i = 0; i < m_serverTable->columnCount(); ++i)
            widths << QString::number(m_serverTable->columnWidth(i));
        CoreSettingsSetValue(SettingsID::Kaillera_ServerColumnWidths, widths.join(",").toStdString());
    }

    // Save geometry
    CoreSettingsSetValue(SettingsID::Kaillera_NetplayGeometry,
        saveGeometry().toBase64().toStdString());
    CoreSettingsSave();
}

void KailleraNetplayDialog::setupUI()
{
    setWindowTitle("RMG-K Netplay");
    setMinimumSize(520, 480);
    resize(580, 530);

    auto* mainLayout = new QVBoxLayout(this);
    // User settings row
    auto* settingsLayout = new QHBoxLayout();
    settingsLayout->addWidget(new QLabel("Username:", this));
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setMaxLength(31);
    settingsLayout->addWidget(m_usernameEdit);
    mainLayout->addLayout(settingsLayout);
    // Only P2P tab
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createP2PTab(), "P2P");
    mainLayout->addWidget(m_tabWidget);
    // Bottom buttons
    auto* bottomLayout = new QHBoxLayout();
    auto* btnAbout = new QPushButton("About", this);
    connect(btnAbout, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this, "About RMG-K Netplay",
            "RMG-K Netplay\n\nKaillera client based on n02 (Open Kaillera)\nP2P mode only.\n\nhttps://github.com/Jay-Day/RMG-K");
    });
    bottomLayout->addWidget(btnAbout);
    bottomLayout->addStretch();
    m_btnClose = new QPushButton("Close", this);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::reject);
    bottomLayout->addWidget(m_btnClose);
    mainLayout->addLayout(bottomLayout);
}

QWidget* KailleraNetplayDialog::createP2PTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    // Sub-tabs: Host | Connect
    auto* subTabs = new QTabWidget(tab);

    // ---- Host sub-tab ----
    auto* hostTab = new QWidget();
    auto* hostLayout = new QVBoxLayout(hostTab);

    // Game name field
    auto* gameLayout = new QHBoxLayout();
    gameLayout->addWidget(new QLabel("Game:", hostTab));
    m_p2pGameEdit = new QLineEdit(hostTab);
    m_p2pGameEdit->setReadOnly(true);
    gameLayout->addWidget(m_p2pGameEdit);
    hostLayout->addLayout(gameLayout);

    // Game list
    m_p2pGameList = new QListWidget(hostTab);
    m_p2pGameList->setStyleSheet("QListWidget { background-color: #3a3a3a; }");

    // Populate from kailleraInfos game list (double-null terminated)
    if (infos.gameList)
    {
        const char* p = infos.gameList;
        while (*p)
        {
            QString gameName = QString::fromUtf8(p);
            m_p2pGameList->addItem(gameName);
            p += strlen(p) + 1;
        }
    }

    // Select first game by default
    if (m_p2pGameList->count() > 0)
    {
        m_p2pGameList->setCurrentRow(0);
        m_p2pGameEdit->setText(m_p2pGameList->item(0)->text());
    }

    connect(m_p2pGameList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (current)
        {
            m_p2pGameEdit->setText(current->text());
        }
    });

    hostLayout->addWidget(m_p2pGameList);

    // Host port + Host button
    auto* hostBtnLayout = new QHBoxLayout();
    hostBtnLayout->addWidget(new QLabel("Host port:", hostTab));
    m_p2pPortEdit = new QLineEdit(hostTab);
    m_p2pPortEdit->setText("27886");
    m_p2pPortEdit->setMaximumWidth(80);
    hostBtnLayout->addWidget(m_p2pPortEdit);
    hostBtnLayout->addStretch();
    m_btnP2PHost = new QPushButton("Host", hostTab);
    connect(m_btnP2PHost, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PHost);
    hostBtnLayout->addWidget(m_btnP2PHost);
    hostLayout->addLayout(hostBtnLayout);

    subTabs->addTab(hostTab, "Host");

    // ---- Connect sub-tab ----
    auto* connectTab = new QWidget();
    auto* connectLayout = new QVBoxLayout(connectTab);

    // Top row: IP/Code field + Connect + Paste & Go
    auto* addrLayout = new QHBoxLayout();
    addrLayout->addWidget(new QLabel("IP/Code:", connectTab));
    m_p2pHostEdit = new QLineEdit(connectTab);
    m_p2pHostEdit->setPlaceholderText("Connect code or ip:port");
    addrLayout->addWidget(m_p2pHostEdit, 1);
    m_btnP2PJoin = new QPushButton("Connect", connectTab);
    connect(m_btnP2PJoin, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PJoin);
    addrLayout->addWidget(m_btnP2PJoin);
    m_btnP2PPasteGo = new QPushButton("Paste && Go", connectTab);
    connect(m_btnP2PPasteGo, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PPasteAndGo);
    addrLayout->addWidget(m_btnP2PPasteGo);
    connectLayout->addLayout(addrLayout);

    m_p2pWaitingGamesTable = new QTableWidget(0, 5, connectTab);
    m_p2pWaitingGamesTable->setHorizontalHeaderLabels({"Game", "Emulator", "User", "Server", "IP"});
    m_p2pWaitingGamesTable->horizontalHeader()->setStretchLastSection(true);
    m_p2pWaitingGamesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_p2pWaitingGamesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_p2pWaitingGamesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connectLayout->addWidget(m_p2pWaitingGamesTable, 1);

    // When a row is selected, fill the IP/Code field
    connect(m_p2pWaitingGamesTable, &QTableWidget::cellClicked, this, [this](int row, int col) {
        Q_UNUSED(col);
        if (row >= 0 && row < m_p2pWaitingGamesTable->rowCount()) {
            QString ip = m_p2pWaitingGamesTable->item(row, 4)->text();
            m_p2pHostEdit->setText(ip);
        }
    });

    // Fetch waiting games when the tab is shown
    QTimer::singleShot(0, this, [this]() { fetchWaitingGames(); });

    subTabs->addTab(connectTab, "Connect");
    layout->addWidget(subTabs);
    return tab;
}

void KailleraNetplayDialog::fetchWaitingGames()
{
    QNetworkRequest request(QUrl("http://kaillerareborn.2manygames.fr/game_list.php"));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_p2pWaitingGamesTable->setRowCount(0);
        QByteArray data = reply->readAll();
        if (data.size() < 50) return;
        QStringList fields = QString::fromUtf8(data).split('|', Qt::SkipEmptyParts);
        for (int i = 0; i + 6 < fields.size(); i += 7) {
            QString gameName = fields[i].trimmed();
            QString hostPort = fields[i + 1].trimmed();
            QString username = fields[i + 2].trimmed();
            QString emulator = fields[i + 3].trimmed();
            QString serverName = fields[i + 5].trimmed();
            // Only show Mupen-MPN games
            if (emulator != "Mupen-MPN") continue;
            // Filter private IPs
            bool isPrivate = false;
            if (hostPort.startsWith("10.") || hostPort.startsWith("192.168.") || hostPort.startsWith("127.")) {
                isPrivate = true;
            } else if (hostPort.startsWith("172.")) {
                QStringList octets = hostPort.split('.');
                if (octets.size() >= 2) {
                    int second = octets[1].toInt();
                    if (second >= 16 && second <= 31)
                        isPrivate = true;
                }
            }
            if (isPrivate) continue;
            int row = m_p2pWaitingGamesTable->rowCount();
            m_p2pWaitingGamesTable->insertRow(row);
            m_p2pWaitingGamesTable->setItem(row, 0, new QTableWidgetItem(gameName));
            m_p2pWaitingGamesTable->setItem(row, 1, new QTableWidgetItem(emulator));
            m_p2pWaitingGamesTable->setItem(row, 2, new QTableWidgetItem(username));
            m_p2pWaitingGamesTable->setItem(row, 3, new QTableWidgetItem(serverName));
            m_p2pWaitingGamesTable->setItem(row, 4, new QTableWidgetItem(hostPort));
        }
    });
}

void KailleraNetplayDialog::loadSettings()
{
    // Load username
    std::string username = CoreSettingsGetStringValue(SettingsID::Kaillera_Username);
    if (username.empty())
    {
        // Fallback to Windows username
        char winUser[32];
        DWORD size = sizeof(winUser);
        if (GetUserNameA(winUser, &size))
        {
            username = winUser;
        }
        else
        {
            username = "Player";
        }
    }
    m_usernameEdit->setText(QString::fromStdString(username));

    // Load frame delay
    int frameDelay = CoreSettingsGetIntValue(SettingsID::Kaillera_SpoofPing);
    if (frameDelay < 0 || frameDelay > 9) frameDelay = 0;
    m_frameDelayCombo->setCurrentIndex(frameDelay);

    // Load active mode and select the corresponding tab
    int mode = CoreSettingsGetIntValue(SettingsID::Kaillera_ActiveMode);
    if (mode < 0 || mode > 2) mode = 0;
    // Tab order: 0=Server, 1=P2P, 2=Playback
    // Mode order: 0=P2P, 1=Server, 2=Playback
    int tabIndex = 0;
    switch (mode)
    {
        case 0: tabIndex = 1; break; // P2P -> tab 1
        case 1: tabIndex = 0; break; // Server -> tab 0
        case 2: tabIndex = 2; break; // Playback -> tab 2
    }
    m_tabWidget->setCurrentIndex(tabIndex);
}

void KailleraNetplayDialog::saveSettings()
{
    CoreSettingsSetValue(SettingsID::Kaillera_Username,
                         m_usernameEdit->text().toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_SpoofPing,
                         m_frameDelayCombo->currentIndex());

    // Tab order: 0=Server, 1=P2P, 2=Playback
    // Mode order: 0=P2P, 1=Server, 2=Playback
    int mode = 1;
    switch (m_tabWidget->currentIndex())
    {
        case 0: mode = 1; break; // Server tab -> mode 1
        case 1: mode = 0; break; // P2P tab -> mode 0
        case 2: mode = 2; break; // Playback tab -> mode 2
    }
    CoreSettingsSetValue(SettingsID::Kaillera_ActiveMode, mode);
}

void KailleraNetplayDialog::loadServerList()
{
    m_servers.clear();

    std::vector<std::string> names = CoreSettingsGetStringListValue(SettingsID::Kaillera_ServerListNames);
    std::vector<std::string> hosts = CoreSettingsGetStringListValue(SettingsID::Kaillera_ServerListHosts);

    if (!names.empty() && names.size() == hosts.size())
    {
        // Load saved server list
        for (size_t i = 0; i < names.size(); i++)
        {
            m_servers.append({
                QString::fromStdString(names[i]),
                QString::fromStdString(hosts[i]),
                "-"
            });
        }
    }
    else
    {
        // First run — seed default servers
        m_servers.append({"Chicago SSB", "92.38.176.115:27888", "-"});
        m_servers.append({"SSBL Georgia Netplay", "45.61.60.96:27888", "-"});
        m_servers.append({"Miami Secret", "185.144.159.190:27888", "-"});
        m_servers.append({"Seattle", "23.227.163.253:27888", "-"});
        m_servers.append({"San Fran", "165.227.60.3:27888", "-"});
    }

    refreshServerListDisplay();
}

void KailleraNetplayDialog::saveServerList()
{
    std::vector<std::string> names;
    std::vector<std::string> hosts;
    names.reserve(m_servers.size());
    hosts.reserve(m_servers.size());

    for (const auto& s : m_servers)
    {
        names.push_back(s.name.toStdString());
        hosts.push_back(s.host.toStdString());
    }

    CoreSettingsSetValue(SettingsID::Kaillera_ServerListNames, names);
    CoreSettingsSetValue(SettingsID::Kaillera_ServerListHosts, hosts);
    CoreSettingsSave();
}

void KailleraNetplayDialog::refreshServerListDisplay()
{
    m_serverTable->setSortingEnabled(false);
    m_serverTable->setRowCount(m_servers.size());
    for (int i = 0; i < m_servers.size(); i++)
    {
        auto* nameItem = new QTableWidgetItem(m_servers[i].name);
        nameItem->setData(Qt::UserRole, i); // store m_servers index
        m_serverTable->setItem(i, 0, nameItem);
        m_serverTable->setItem(i, 1, new QTableWidgetItem(m_servers[i].host));
        m_serverTable->setItem(i, 2, new QTableWidgetItem(m_servers[i].ping));
    }
    m_serverTable->setSortingEnabled(true);
}

int KailleraNetplayDialog::serverIndexFromRow(int row)
{
    if (row < 0 || row >= m_serverTable->rowCount()) return -1;
    QTableWidgetItem* item = m_serverTable->item(row, 0);
    if (!item) return -1;
    return item->data(Qt::UserRole).toInt();
}

void KailleraNetplayDialog::pingServerRow(int row)
{
    int idx = serverIndexFromRow(row);
    if (idx < 0 || idx >= m_servers.size()) return;

    // Parse host:port
    QString hostStr = m_servers[idx].host;
    QByteArray ipBytes;
    int port = 27888;
    int colonIdx = hostStr.lastIndexOf(':');
    if (colonIdx >= 0)
    {
        ipBytes = hostStr.left(colonIdx).toUtf8();
        port = hostStr.mid(colonIdx + 1).toInt();
        if (port == 0) port = 27888;
    }
    else
    {
        ipBytes = hostStr.toUtf8();
    }

    // Show pinging state
    m_servers[idx].ping = "...";
    refreshServerListDisplay();
    m_serverTable->repaint();

    // Use n02's built-in ping function (2 second timeout)
    int pingMs = kaillera_ping_server(ipBytes.data(), port, 2000);
    if (pingMs >= 0)
    {
        m_servers[idx].ping = QString::number(pingMs) + "ms";
    }
    else
    {
        m_servers[idx].ping = "timeout";
    }

    refreshServerListDisplay();
}

void KailleraNetplayDialog::onStateMachineTimer()
{
    // Detect playback ending naturally (recording ran out).
    // player_EndGame() sets player_playing=false and KSSDFA.state=0 from the
    // emulation thread. If we were tracking an active playback and it just
    // went inactive, stop emulation.
    if (m_playbackWasActive && !n02::isPlaybackActive())
    {
        m_playbackWasActive = false;
        CoreMarkKailleraGameInactive();
        CoreStopEmulation();
    }

    // Drive the KSSDFA state machine one step (non-blocking)
    bool active = n02::processStateMachineStep();
    if (!active)
    {
        // State 3 = shutdown, close dialog
        m_stateMachineTimer->stop();
        accept();
    }
}

void KailleraNetplayDialog::onTabChanged(int index)
{
    // Tab order: 0=Server, 1=P2P, 2=Playback
    // n02 mode: 0=P2P, 1=Server, 2=Playback
    int mode = 1;
    switch (index)
    {
        case 0: mode = 1; break;
        case 1: mode = 0; break;
        case 2: mode = 2; break;
    }
    n02::activateMode(mode);
}

void KailleraNetplayDialog::onAddServer()
{
    bool ok;
    QString name = QInputDialog::getText(this, "Add Server", "Server Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    QString host = QInputDialog::getText(this, "Add Server", "Host (ip:port):", QLineEdit::Normal, "127.0.0.1:27888", &ok);
    if (!ok || host.isEmpty()) return;

    m_servers.append({name, host, "-"});
    refreshServerListDisplay();
}

void KailleraNetplayDialog::onEditServer()
{
    int row = m_serverTable->currentRow();
    int idx = serverIndexFromRow(row);
    if (idx < 0 || idx >= m_servers.size()) return;

    bool ok;
    QString name = QInputDialog::getText(this, "Edit Server", "Server Name:",
                                         QLineEdit::Normal, m_servers[idx].name, &ok);
    if (!ok) return;

    QString host = QInputDialog::getText(this, "Edit Server", "Host (ip:port):",
                                         QLineEdit::Normal, m_servers[idx].host, &ok);
    if (!ok) return;

    m_servers[idx].name = name;
    m_servers[idx].host = host;
    m_servers[idx].ping = "-";
    refreshServerListDisplay();
}

void KailleraNetplayDialog::onDeleteServer()
{
    int row = m_serverTable->currentRow();
    int idx = serverIndexFromRow(row);
    if (idx < 0 || idx >= m_servers.size()) return;

    m_servers.removeAt(idx);
    refreshServerListDisplay();
}

void KailleraNetplayDialog::onServerRightClicked(QPoint pos)
{
    int row = m_serverTable->rowAt(pos.y());
    int idx = serverIndexFromRow(row);
    if (idx < 0 || idx >= m_servers.size()) return;

    m_serverTable->selectRow(row);

    QMenu menu(this);
    QAction* actPing = menu.addAction("Ping");
    QAction* actTraceroute = menu.addAction("Traceroute");

    QAction* chosen = menu.exec(m_serverTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actPing)
    {
        pingServerRow(row);
    }
    else if (chosen == actTraceroute)
    {
        // Extract IP (strip port)
        QString host = m_servers[idx].host;
        QString ip = host.split(':').first();

        // Launch tracert in a new console window
        QString cmd = "cmd.exe /c \"tracert " + ip + " & pause\"";
        QByteArray cmdBytes = cmd.toLocal8Bit();
        WinExec(cmdBytes.constData(), SW_SHOW);
    }
}

void KailleraNetplayDialog::onLiveServerList()
{
    // Open the live server list as a separate dialog
    QDialog* liveDialog = new QDialog(this);
    liveDialog->setWindowTitle("Live Server List");
    liveDialog->setMinimumSize(600, 400);
    liveDialog->resize(700, 500);

    auto* dlgLayout = new QVBoxLayout(liveDialog);

    auto* liveLabel = new QLabel("Downloading server list...", liveDialog);
    liveLabel->setStyleSheet("color: blue;");
    dlgLayout->addWidget(liveLabel);

    auto* liveTable = new QTableWidget(0, 6, liveDialog);
    liveTable->setHorizontalHeaderLabels({"Name", "IP", "Users", "Games", "Version", "Location"});
    liveTable->horizontalHeader()->setStretchLastSection(true);
    liveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    liveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    liveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    liveTable->setSortingEnabled(true);
    liveTable->horizontalHeader()->setMinimumSectionSize(16);
    dlgLayout->addWidget(liveTable);

    auto* liveBtnLayout = new QHBoxLayout();
    auto* btnAddToList = new QPushButton("Add to Server List", liveDialog);
    auto* btnLiveConnect = new QPushButton("Connect", liveDialog);
    auto* btnLiveClose = new QPushButton("Close", liveDialog);
    liveBtnLayout->addWidget(btnAddToList);
    liveBtnLayout->addWidget(btnLiveConnect);
    liveBtnLayout->addStretch();
    liveBtnLayout->addWidget(btnLiveClose);
    dlgLayout->addLayout(liveBtnLayout);

    connect(btnLiveClose, &QPushButton::clicked, liveDialog, &QDialog::accept);

    // "Add to Server List" — adds selected server to the main server table
    connect(btnAddToList, &QPushButton::clicked, this, [this, liveTable, liveDialog]() {
        int row = liveTable->currentRow();
        if (row < 0) return;

        QString name = liveTable->item(row, 0)->text();
        QString hostPort = liveTable->item(row, 1)->text();

        for (const auto& s : m_servers)
        {
            if (s.host == hostPort)
            {
                QMessageBox::information(liveDialog, "Already Added",
                    "This server is already in your list.");
                return;
            }
        }

        m_servers.append({name, hostPort, "-"});
        refreshServerListDisplay();
    });

    // "Connect" — add to list if needed and connect
    connect(btnLiveConnect, &QPushButton::clicked, this, [this, liveTable, liveDialog]() {
        int row = liveTable->currentRow();
        if (row < 0) return;

        QString name = liveTable->item(row, 0)->text();
        QString hostPort = liveTable->item(row, 1)->text();

        // Add to server list if not already there
        bool found = false;
        for (int i = 0; i < m_servers.size(); i++)
        {
            if (m_servers[i].host == hostPort)
            {
                m_serverTable->selectRow(i);
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_servers.append({name, hostPort, "-"});
            refreshServerListDisplay();
            m_serverTable->selectRow(m_servers.size() - 1);
        }

        liveDialog->accept();
        onConnectServer();
    });

    // Fetch the master server list
    QNetworkRequest request(QUrl("http://kaillerareborn.2manygames.fr/server_list.php"));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, liveTable, liveLabel]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            liveLabel->setText("Error: " + reply->errorString());
            liveLabel->setStyleSheet("color: red;");
            return;
        }

        QByteArray data = reply->readAll();
        if (data.size() < 150)
        {
            liveLabel->setText("Error: server list too small or empty");
            liveLabel->setStyleSheet("color: red;");
            return;
        }

        // Parse response: ServerName\nIP:Port;Users;Games;Version;Location\n (repeating)
        const char* ptr = data.constData();
        const char* end = ptr + data.size();
        int total = 0;
        liveTable->setSortingEnabled(false);

        while (ptr < end - 10)
        {
            // Server name (until \n)
            const char* nameStart = ptr;
            while (ptr < end && *ptr != '\n') ptr++;
            if (ptr >= end) break;
            QString name = QString::fromUtf8(nameStart, ptr - nameStart).trimmed();
            ptr++; // skip \n

            // IP:Port;Users;Games;Version;Location (until \n)
            const char* lineStart = ptr;
            while (ptr < end && *ptr != '\n') ptr++;
            if (ptr >= end && ptr == lineStart) break;
            QString line = QString::fromUtf8(lineStart, ptr - lineStart).trimmed();
            if (ptr < end) ptr++; // skip \n

            if (name.isEmpty() || line.isEmpty()) continue;

            QStringList parts = line.split(';');
            if (parts.size() < 1) continue;

            QString hostPort = parts[0];
            QString users = parts.size() > 1 ? parts[1] : "";
            QString games = parts.size() > 2 ? parts[2] : "";
            QString version = parts.size() > 3 ? parts[3] : "";
            QString location = parts.size() > 4 ? parts[4] : "";

            // Filter private IPs (10.x, 127.x, 192.168.x, 172.16-31.x)
            {
                bool isPrivate = false;
                if (hostPort.startsWith("10.") || hostPort.startsWith("192.168.") ||
                    hostPort.startsWith("127."))
                {
                    isPrivate = true;
                }
                else if (hostPort.startsWith("172."))
                {
                    QStringList octets = hostPort.split('.');
                    if (octets.size() >= 2)
                    {
                        int second = octets[1].toInt();
                        if (second >= 16 && second <= 31)
                            isPrivate = true;
                    }
                }
                if (isPrivate) continue;
            }

            int row = liveTable->rowCount();
            liveTable->insertRow(row);
            liveTable->setItem(row, 0, new QTableWidgetItem(name));
            liveTable->setItem(row, 1, new QTableWidgetItem(hostPort));
            auto* usersItem = new QTableWidgetItem(users);
            usersItem->setData(Qt::UserRole, users.toInt());
            liveTable->setItem(row, 2, usersItem);
            auto* gamesItem = new QTableWidgetItem(games);
            gamesItem->setData(Qt::UserRole, games.toInt());
            liveTable->setItem(row, 3, gamesItem);
            liveTable->setItem(row, 4, new QTableWidgetItem(version));
            liveTable->setItem(row, 5, new QTableWidgetItem(location));
            total++;
        }

        liveTable->setSortingEnabled(true);
        liveLabel->setText(QString::number(total) + " servers found");
        liveLabel->setStyleSheet("color: green;");
    });

    liveDialog->exec();
    delete liveDialog;
}

void KailleraNetplayDialog::onWaitingGames()
{
    m_btnWaitingGames->setEnabled(false);

    QNetworkRequest request(QUrl("http://kaillerareborn.2manygames.fr/game_list.php"));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onWaitingGamesReply(reply);
    });
}

void KailleraNetplayDialog::onWaitingGamesReply(QNetworkReply* reply)
{
    m_btnWaitingGames->setEnabled(true);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::warning(this, "Waiting Games", "Error: " + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    if (data.size() < 50)
    {
        QMessageBox::information(this, "Waiting Games", "No waiting games found.");
        return;
    }

    // Build popup dialog
    QDialog* wgDialog = new QDialog(this);
    wgDialog->setWindowTitle("Waiting Games");
    wgDialog->setMinimumSize(600, 400);
    wgDialog->resize(700, 450);

    auto* wgLayout = new QVBoxLayout(wgDialog);

    auto* wgTable = new QTableWidget(0, 5, wgDialog);
    wgTable->setHorizontalHeaderLabels({"Game", "Emulator", "User", "Server", "IP"});
    wgTable->horizontalHeader()->setStretchLastSection(true);
    wgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    wgTable->setSelectionMode(QAbstractItemView::SingleSelection);
    wgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    wgTable->setSortingEnabled(true);
    wgTable->horizontalHeader()->setMinimumSectionSize(16);
    wgLayout->addWidget(wgTable);

    auto* wgBtnLayout = new QHBoxLayout();
    auto* btnAddToList = new QPushButton("Add Server to List", wgDialog);
    auto* btnWgClose = new QPushButton("Close", wgDialog);
    wgBtnLayout->addWidget(btnAddToList);
    wgBtnLayout->addStretch();
    wgBtnLayout->addWidget(btnWgClose);
    wgLayout->addLayout(wgBtnLayout);

    connect(btnWgClose, &QPushButton::clicked, wgDialog, &QDialog::accept);

    // Parse: pipe-delimited fields, 7 per entry
    // GameName|IP:Port|Username|EmuName|WaitingPlayers|ServerName|ServerLocation|...
    QStringList fields = QString::fromUtf8(data).split('|', Qt::SkipEmptyParts);

    int total = 0;
    wgTable->setSortingEnabled(false);
    for (int i = 0; i + 6 < fields.size(); i += 7)
    {
        QString gameName = fields[i].trimmed();
        QString hostPort = fields[i + 1].trimmed();
        QString username = fields[i + 2].trimmed();
        QString emulator = fields[i + 3].trimmed();
        QString serverName = fields[i + 5].trimmed();

        // Filter private IPs (10.x, 127.x, 192.168.x, 172.16-31.x)
        {
            bool isPrivate = false;
            if (hostPort.startsWith("10.") || hostPort.startsWith("192.168.") ||
                hostPort.startsWith("127."))
            {
                isPrivate = true;
            }
            else if (hostPort.startsWith("172."))
            {
                QStringList octets = hostPort.split('.');
                if (octets.size() >= 2)
                {
                    int second = octets[1].toInt();
                    if (second >= 16 && second <= 31)
                        isPrivate = true;
                }
            }
            if (isPrivate) continue;
        }

        int row = wgTable->rowCount();
        wgTable->insertRow(row);
        wgTable->setItem(row, 0, new QTableWidgetItem(gameName));
        wgTable->setItem(row, 1, new QTableWidgetItem(emulator));
        wgTable->setItem(row, 2, new QTableWidgetItem(username));
        wgTable->setItem(row, 3, new QTableWidgetItem(serverName));
        wgTable->setItem(row, 4, new QTableWidgetItem(hostPort));
        total++;
    }

    wgTable->setSortingEnabled(true);

    connect(btnAddToList, &QPushButton::clicked, this, [this, wgTable, wgDialog]() {
        int row = wgTable->currentRow();
        if (row < 0) return;

        QString serverName = wgTable->item(row, 3)->text();
        QString hostPort = wgTable->item(row, 4)->text();

        for (const auto& s : m_servers)
        {
            if (s.host == hostPort)
            {
                QMessageBox::information(wgDialog, "Already Added",
                    "This server is already in your list.");
                return;
            }
        }

        m_servers.append({serverName, hostPort, "-"});
        refreshServerListDisplay();
    });

    wgDialog->exec();
    delete wgDialog;
}

void KailleraNetplayDialog::onConnectServer()
{
    int row = m_serverTable->currentRow();
    int idx = serverIndexFromRow(row);
    if (idx < 0 || idx >= m_servers.size()) return;

    // Parse host:port
    QString hostStr = m_servers[idx].host;
    QByteArray ipBytes;
    int port = 27888;
    int colonIdx = hostStr.lastIndexOf(':');
    if (colonIdx >= 0)
    {
        ipBytes = hostStr.left(colonIdx).toUtf8();
        port = hostStr.mid(colonIdx + 1).toInt();
        if (port == 0) port = 27888;
    }
    else
    {
        ipBytes = hostStr.toUtf8();
    }

    // Set spoof ping from frame delay combo
    int fdlyIndex = m_frameDelayCombo->currentIndex();
    if (fdlyIndex > 0 && fdlyIndex <= 9)
    {
        kaillera_set_spoof_ping(fdlyIndex * 16 - 8);
    }
    else
    {
        kaillera_set_spoof_ping(0);
    }

    // Get username
    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    // Initialize kaillera core for server mode
    if (kaillera_core_initialize(0, APP, usernameBytes.data(), 1))
    {
        // Run connect on a background thread so the UI stays responsive
        // (kaillera_core_connect blocks for up to 15 seconds on timeout)
        auto connectFuture = std::async(std::launch::async, [&]() {
            return kaillera_core_connect(ipBytes.data(), port);
        });

        // Show a progress dialog while connecting
        QProgressDialog progress("Connecting to " + m_servers[idx].name + "...",
                                 "Cancel", 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();

        // Poll until the future completes or user cancels
        while (connectFuture.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
        {
            QApplication::processEvents();
            if (progress.wasCanceled())
            {
                // Can't cancel the blocking socket wait, so keep processing events until it finishes
                while (connectFuture.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
                    QApplication::processEvents();
                kaillera_core_cleanup();
                return;
            }
        }
        progress.close();

        if (connectFuture.get())
        {
            // Hide the netplay dialog while the server browser is open
            hide();

            // Open the server browser dialog as a standalone top-level window
            // so it doesn't stay on top of the emulator frame
            KailleraServerBrowserDialog browser(m_servers[idx].name, nullptr);
            browser.show();

            QEventLoop loop;
            connect(&browser, &QDialog::finished, &loop, &QEventLoop::quit);
            loop.exec();
            // Browser dialog handles disconnect/cleanup on close

            // Re-show the netplay dialog
            show();
        }
        else
        {
            QString errorMsg = QString::fromUtf8(kaillera_core_get_last_error());
            kaillera_core_cleanup();
            if (errorMsg.isEmpty())
                errorMsg = "Failed to connect to server";
            QMessageBox::warning(this, "Connection Error",
                                 errorMsg + "\n\nServer: " + m_servers[idx].name);
        }
    }
    else
    {
        QMessageBox::warning(this, "Connection Error", "Failed to initialize Kaillera core.");
    }
}

void KailleraNetplayDialog::onServerDoubleClicked(int row, int column)
{
    (void)column;
    int idx = serverIndexFromRow(row);
    if (idx >= 0 && idx < m_servers.size())
    {
        onConnectServer();
    }
}

void KailleraNetplayDialog::onP2PHost()
{
    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    // Use selected game from game list
    QString gameName = m_p2pGameEdit->text().trimmed();
    if (gameName.isEmpty())
    {
        QMessageBox::warning(this, "P2P Host", "No game selected. Please select a game from the list.");
        return;
    }
    QByteArray gameBytes = gameName.toUtf8();

    int port = m_p2pPortEdit->text().toInt();
    if (port <= 0 || port > 65535) port = 27886;

    if (p2p_core_initialize(true, port, APP, gameBytes.data(), usernameBytes.data()))
    {
        hide();

        QString username = QString::fromUtf8(usernameBytes);
        KailleraP2PDialog p2pDialog(true, gameName, username, QString(), nullptr);
        p2pDialog.show();

        QEventLoop loop;
        connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();

        show();
    }
    else
    {
        QMessageBox::warning(this, "P2P Host", "Failed to initialize P2P core.");
    }
}

// Check if a string looks like a NAT traversal code rather than an IP address.
static bool looksLikeTraversalCode(const QString& s)
{
    if (s.isEmpty()) return false;
    for (const QChar& ch : s)
    {
        if (ch == '.' || ch == ':' || ch == '/') return false;
    }
    int alnumCount = 0;
    for (const QChar& ch : s)
    {
        if (ch.isLetterOrNumber()) { alnumCount++; continue; }
        if (ch == '-' || ch == '_') continue;
        return false;
    }
    return (alnumCount >= 6 && alnumCount <= 16);
}

void KailleraNetplayDialog::onP2PJoin()
{
    QString addrText = m_p2pHostEdit->text().trimmed();
    if (addrText.isEmpty())
    {
        QMessageBox::warning(this, "P2P Join", "Please enter a connect code or host address (ip:port).");
        return;
    }

    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    bool isCode = looksLikeTraversalCode(addrText);

    if (p2p_core_initialize(false, 0, APP, (char*)"", usernameBytes.data()))
    {
        if (isCode)
        {
            // Join by traversal code — the dialog handles connecting via NAT traversal
            hide();

            QString username = QString::fromUtf8(usernameBytes);
            KailleraP2PDialog p2pDialog(false, QString(), username, addrText, nullptr);
            p2pDialog.show();

            QEventLoop loop;
            connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
            loop.exec();

            show();
        }
        else
        {
            // Join by direct IP:port
            QByteArray ipBytes;
            int port = 27886;
            int colonIdx = addrText.lastIndexOf(':');
            if (colonIdx >= 0)
            {
                ipBytes = addrText.left(colonIdx).toUtf8();
                port = addrText.mid(colonIdx + 1).toInt();
                if (port == 0) port = 27886;
            }
            else
            {
                ipBytes = addrText.toUtf8();
            }

            if (p2p_core_connect(ipBytes.data(), port))
            {
                hide();

                QString username = QString::fromUtf8(usernameBytes);
                KailleraP2PDialog p2pDialog(false, QString(), username, QString(), nullptr);
                p2pDialog.show();

                QEventLoop loop;
                connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
                loop.exec();

                show();
            }
            else
            {
                p2p_core_cleanup();
                QMessageBox::warning(this, "P2P Join", "Failed to connect to host: " + addrText);
            }
        }
    }
    else
    {
        QMessageBox::warning(this, "P2P Join", "Failed to initialize P2P core.");
    }
}

void KailleraNetplayDialog::onP2PPasteAndGo()
{
    QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.isEmpty()) return;

    // Remove spaces (connect codes shouldn't have spaces)
    clip.remove(' ');

    m_p2pHostEdit->setText(clip);
    onP2PJoin();
}

void KailleraNetplayDialog::populatePlaybackList()
{
    if (!m_playbackTable) return;

    m_playbackTable->setSortingEnabled(false);
    m_playbackTable->setRowCount(0);

    QDir recordsDir("./records");
    if (!recordsDir.exists())
    {
        QDir(".").mkdir("records");
        m_playbackTable->setSortingEnabled(true);
        return;
    }

    QStringList filters;
    filters << "*.krec";
    QFileInfoList files = recordsDir.entryInfoList(filters, QDir::Files, QDir::Name | QDir::Reversed);

    for (const QFileInfo& fi : files)
    {
        std::string fullPath = fi.absoluteFilePath().toStdString();
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) continue;

        std::streamsize len = file.tellg();
        if (len < 272)
        {
            file.close();
            continue;
        }

        file.seekg(0, std::ios::beg);
        char* filebuf = (char*)malloc((size_t)len + 1);
        if (!filebuf) { file.close(); continue; }
        file.read(filebuf, len);
        file.close();

        char VER[5];
        memcpy(VER, filebuf, 4);
        VER[4] = 0;
        bool isKRC1 = (strcmp(VER, "KRC1") == 0);
        if (strcmp(VER, "KRC0") != 0 && !isKRC1)
        {
            free(filebuf);
            continue;
        }

        size_t headerSize = isKRC1 ? 400 : 272;
        if ((size_t)len < headerSize)
        {
            free(filebuf);
            continue;
        }

        // Parse game name (offset 132, 128 bytes)
        char gameName[129];
        memcpy(gameName, filebuf + 132, 128);
        gameName[128] = 0;

        // Parse timestamp, player number, numplayers (offset 260)
        int32_t timestamp = 0;
        memcpy(&timestamp, filebuf + 260, 4);
        int32_t recPlayerNo = 0;
        memcpy(&recPlayerNo, filebuf + 264, 4);
        int32_t recNumPlayers = 0;
        memcpy(&recNumPlayers, filebuf + 268, 4);

        // Column 0: Date
        QString dateStr;
        {
            bool parsed = false;
            QByteArray fn = fi.fileName().toUtf8();
            if (!isKRC1 && fn.size() > 13)
            {
                bool allDigits = true;
                for (int d = 0; d < 12; d++)
                {
                    if (!isdigit((unsigned char)fn[d])) { allDigits = false; break; }
                }
                if (allDigits)
                {
                    // YYMMDDHHMMSS format
                    dateStr = QString("%1/%2/%3 %4:%5")
                        .arg(QString::fromUtf8(fn.mid(0, 2)))
                        .arg(QString::fromUtf8(fn.mid(2, 2)))
                        .arg(QString::fromUtf8(fn.mid(4, 2)))
                        .arg(QString::fromUtf8(fn.mid(6, 2)))
                        .arg(QString::fromUtf8(fn.mid(8, 2)));
                    parsed = true;
                }
            }
            if (!parsed)
            {
                time_t t = (time_t)timestamp;
                tm* lt = localtime(&t);
                if (lt)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%02d/%02d/%02d %02d:%02d",
                             lt->tm_year % 100, lt->tm_mon + 1, lt->tm_mday,
                             lt->tm_hour, lt->tm_min);
                    dateStr = QString::fromUtf8(buf);
                }
                else
                {
                    dateStr = "?";
                }
            }
        }

        // Column 1: Players
        QString playersStr;
        {
            if (isKRC1)
            {
                QStringList names;
                for (int p = 0; p < recNumPlayers && p < 4; p++)
                {
                    char name[33];
                    memcpy(name, filebuf + 272 + p * 32, 32);
                    name[32] = 0;
                    if (name[0] != 0)
                        names.append(QString::fromUtf8(name));
                }
                playersStr = names.isEmpty() ? "?" : names.join(", ");
            }
            else
            {
                // KRC0: try to parse player names from filename
                QByteArray fn = fi.fileName().toUtf8();
                int nameStart = 0;
                if (fn.size() > 13)
                {
                    bool allDigits = true;
                    for (int d = 0; d < 12; d++)
                    {
                        if (!isdigit((unsigned char)fn[d])) { allDigits = false; break; }
                    }
                    if (allDigits && fn[12] == '-') nameStart = 13;
                }
                if (nameStart > 0)
                {
                    int extIdx = fn.indexOf(".krec");
                    if (extIdx < 0) extIdx = fn.size();
                    // Find last dash before extension — that separates players from game
                    int lastDash = -1;
                    for (int s = nameStart; s < extIdx; s++)
                    {
                        if (fn[s] == '-') lastDash = s;
                    }
                    if (lastDash > nameStart)
                    {
                        QByteArray playerPart = fn.mid(nameStart, lastDash - nameStart);
                        playersStr = QString::fromUtf8(playerPart).replace('-', ", ");
                    }
                    else
                    {
                        playersStr = "?";
                    }
                }
                else
                {
                    playersStr = "?";
                }
            }
        }

        // Column 3: Duration — count 0x12 records
        int frames = 0;
        {
            char* scan = filebuf + headerSize;
            char* scanEnd = filebuf + len;
            while (scan + 1 < scanEnd)
            {
                unsigned char type = (unsigned char)*scan++;
                if (type == 0x12)
                {
                    if (scan + 2 > scanEnd) break;
                    unsigned short rlen = *(unsigned short*)scan;
                    scan += 2;
                    if (rlen > 0)
                    {
                        if (scan + rlen > scanEnd) break;
                        scan += rlen;
                    }
                    frames++;
                }
                else if (type == 0x14)
                {
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                    scan += 4;
                }
                else if (type == 0x08)
                {
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                }
                else
                {
                    break;
                }
            }
        }
        int totalSec = frames / 60;
        int mins = totalSec / 60;
        int secs = totalSec % 60;
        QString durationStr = QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));

        // Column 4: Size
        QString sizeStr;
        {
            qint64 sz = fi.size();
            if (sz <= 1024)
                sizeStr = QString::number(sz) + " B";
            else if (sz < 1024 * 1000)
                sizeStr = QString::number(sz / 1024) + " kB";
            else
                sizeStr = QString("%1.%2 MB").arg(sz / (1024 * 1000)).arg((sz % (1024 * 1000)) / (1024 * 100));
        }

        // Add row
        int row = m_playbackTable->rowCount();
        m_playbackTable->insertRow(row);
        m_playbackTable->setItem(row, 0, new QTableWidgetItem(dateStr));
        m_playbackTable->setItem(row, 1, new QTableWidgetItem(playersStr));
        m_playbackTable->setItem(row, 2, new QTableWidgetItem(QString::fromUtf8(gameName)));
        m_playbackTable->setItem(row, 3, new QTableWidgetItem(durationStr));
        m_playbackTable->setItem(row, 4, new QTableWidgetItem(sizeStr));
        m_playbackTable->setItem(row, 5, new QTableWidgetItem(fi.fileName()));

        free(filebuf);
    }

    m_playbackTable->setSortingEnabled(true);

    // Default sort: date descending (newest first)
    m_playbackTable->sortByColumn(0, Qt::DescendingOrder);
}

void KailleraNetplayDialog::onPlaybackPlay()
{
    if (!m_playbackTable) return;
    if (n02::isPlaybackActive()) return;

    int row = m_playbackTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* fnItem = m_playbackTable->item(row, 5);
    if (!fnItem) return;

    QString filename = "./records/" + fnItem->text();
    QByteArray pathBytes = filename.toUtf8();

    // Ensure playback mode is active
    n02::activateMode(2);

    if (n02::playbackLoad(pathBytes.constData()))
    {
        m_playbackWasActive = true;
    }
    else
    {
        QMessageBox::warning(this, "Playback", "Failed to load recording: " + fnItem->text());
    }
}

void KailleraNetplayDialog::onPlaybackStop()
{
    if (n02::isPlaybackActive())
    {
        n02::endGame();
    }
    // n02::endGame() transitions the state machine but doesn't stop emulation.
    // Directly stop the emulator so playback actually ends.
    m_playbackWasActive = false;
    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
}

void KailleraNetplayDialog::onPlaybackDelete()
{
    if (!m_playbackTable) return;

    int row = m_playbackTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* fnItem = m_playbackTable->item(row, 5);
    if (!fnItem) return;

    QString filename = fnItem->text();
    if (QMessageBox::question(this, "Delete Recording",
            "Delete \"" + filename + "\"?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QString fullPath = "./records/" + filename;
    QFile::remove(fullPath);
    populatePlaybackList();
}

void KailleraNetplayDialog::onPlaybackRefresh()
{
    populatePlaybackList();
}

void KailleraNetplayDialog::onPlaybackOpenFolder()
{
    QDir(".").mkdir("records");
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir("./records").absolutePath()));
}

void KailleraNetplayDialog::onPlaybackDoubleClicked(int row, int column)
{
    (void)column;
    if (row >= 0)
    {
        onPlaybackPlay();
    }
}

#endif // _WIN32
