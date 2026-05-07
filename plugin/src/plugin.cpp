#include <binaryninjaapi.h>
#include <ui/sidebar.h>
#include <ui/uitypes.h>
#include <ui/viewframe.h>

#include "GhidraConnection.h"
#include "ui/ProjectPanel.h"

#include <QFont>
#include <QImage>
#include <QPainter>

using namespace BinaryNinja;

// ---------------------------------------------------------------------------
// Sidebar type — registered once; BN creates one ProjectPanel per pane
// ---------------------------------------------------------------------------

static QImage makeSidebarIcon() {
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    // Red circle background matching Ghidra's colour scheme
    p.setBrush(QColor(0xcc, 0x33, 0x33));
    p.setPen(Qt::NoPen);
    p.drawEllipse(2, 2, 44, 44);
    // White "G"
    QFont f("Arial", 26, QFont::Bold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(img.rect(), Qt::AlignCenter, "G");
    p.end();
    return img;
}

class GhidraSidebarType : public SidebarWidgetType {
public:
    GhidraSidebarType()
        : SidebarWidgetType(makeSidebarIcon(), "Ghidra") {}

    SidebarWidget* createWidget(ViewFrame* /*frame*/, BinaryViewRef /*data*/) override {
        return new ProjectPanel();
    }
};

// ---------------------------------------------------------------------------
// Plugin init
// ---------------------------------------------------------------------------

BN_DECLARE_CORE_ABI_VERSION

extern "C" BINARYNINJAPLUGIN bool CorePluginInit() {
    // ---- Register settings -------------------------------------------------
    auto settings = Settings::Instance();

    settings->RegisterGroup("ghidra", "Ghidra Integration");

    settings->RegisterSetting("ghidra.javaExe", R"({
        "title"       : "Java Executable",
        "type"        : "string",
        "default"     : "java",
        "description" : "Path to the java executable. Leave blank to use 'java' from PATH.",
        "ignore"      : ["SettingsProjectScope", "SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.ghidraHome", R"({
        "title"       : "Ghidra Installation Directory",
        "type"        : "string",
        "default"     : "",
        "description" : "Root directory of your Ghidra installation (contains Ghidra/Framework/...).",
        "ignore"      : ["SettingsProjectScope", "SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.bridgeJar", R"({
        "title"       : "Bridge JAR Path",
        "type"        : "string",
        "default"     : "",
        "description" : "Path to ghidra-bridge-*.jar. Leave blank to look next to this plugin.",
        "ignore"      : ["SettingsProjectScope", "SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.trustAllCerts", R"({
        "title"       : "Trust All SSL Certificates",
        "type"        : "boolean",
        "default"     : false,
        "description" : "Disable SSL certificate validation. Use only on trusted internal networks.",
        "ignore"      : ["SettingsProjectScope", "SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.autoStartBridge", R"({
        "title"       : "Auto-start Bridge on Launch",
        "type"        : "boolean",
        "default"     : true,
        "description" : "Start the Ghidra bridge process automatically when Binary Ninja launches. If disabled, the bridge starts on first Connect.",
        "ignore"      : ["SettingsProjectScope", "SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.defaultHost", R"({
        "title"       : "Default Server Host",
        "type"        : "string",
        "default"     : "localhost",
        "description" : "Pre-fill the Connect dialog with this host.",
        "ignore"      : ["SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.defaultPort", R"({
        "title"       : "Default Server Port",
        "type"        : "number",
        "default"     : 13100,
        "description" : "Pre-fill the Connect dialog with this port.",
        "ignore"      : ["SettingsResourceScope"]
    })");

    settings->RegisterSetting("ghidra.defaultUser", R"({
        "title"       : "Default Username",
        "type"        : "string",
        "default"     : "",
        "description" : "Pre-fill the Connect dialog with this username.",
        "ignore"      : ["SettingsResourceScope"]
    })");

    // ---- Register sidebar --------------------------------------------------
    Sidebar::addSidebarWidgetType(new GhidraSidebarType());

    // ---- Eagerly start bridge in background --------------------------------
    // The JVM takes 1-3 s to boot; starting now means it's ready by the time
    // the user opens the sidebar and clicks Connect.
    if (settings->Get<bool>("ghidra.autoStartBridge")) {
        WorkerEnqueue([]{
            auto& conn = GhidraConnection::instance();
            std::string err;
            if (!conn.startBridge(err))
                LogWarn("Ghidra bridge failed to start: %s", err.c_str());
        });
    }

    return true;
}
