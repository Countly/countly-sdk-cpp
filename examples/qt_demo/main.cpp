#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <functional>
#include <mutex>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QFrame>
#include <QListWidget>
#include <QSpinBox>
#include <QTimer>
#include <QMetaObject>
#include <QFont>
#include <QMessageBox>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>

#include "countly.hpp"
#include "dev_config.hpp"

using json = nlohmann::json;
using namespace cly;

// ---------------------------------------------------------------------------
// Global stylesheet
// ---------------------------------------------------------------------------
static const char *GLOBAL_STYLE = R"(
    QMainWindow, QWidget {
        background: #f5f5f5;
        color: #333;
    }
    QTabWidget::pane {
        border: 1px solid #ddd;
        background: #ffffff;
        border-radius: 4px;
        margin-top: -1px;
    }
    QTabBar {
        background: #2c3e50;
    }
    QTabBar::tab {
        background: #34495e;
        color: #bdc3c7;
        border: none;
        padding: 10px 20px;
        margin-right: 1px;
        font-size: 13px;
        font-weight: 500;
        min-width: 80px;
    }
    QTabBar::tab:selected {
        background: #ffffff;
        color: #2c3e50;
        font-weight: bold;
        border-top: 3px solid #27ae60;
        padding-top: 7px;
    }
    QTabBar::tab:hover:!selected {
        background: #4a6785;
        color: #ecf0f1;
    }
    QGroupBox {
        font-weight: bold;
        font-size: 13px;
        border: 1px solid #ddd;
        border-radius: 6px;
        margin-top: 12px;
        padding-top: 18px;
        background: #fafafa;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 12px;
        padding: 0 6px;
        color: #444;
    }
    QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox {
        border: 1px solid #ccc;
        border-radius: 4px;
        padding: 6px 8px;
        background: #ffffff;
        color: #222;
        font-size: 13px;
    }
    QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus {
        border-color: #4A90D9;
    }
    QCheckBox {
        font-size: 13px;
        spacing: 6px;
    }
    QLabel {
        font-size: 13px;
        color: #333;
    }
    QPushButton {
        border: 1px solid #ccc;
        border-radius: 4px;
        padding: 8px 16px;
        font-size: 13px;
        background: #f0f0f0;
        color: #333;
    }
    QPushButton:hover {
        background: #e0e0e0;
    }
    QPushButton:pressed {
        background: #d0d0d0;
    }
    QListWidget {
        border: 1px solid #ccc;
        border-radius: 4px;
        background: #ffffff;
        font-size: 13px;
    }
)";

static const QString BTN_GREEN =
    "QPushButton { background: #27ae60; color: white; border-color: #219a52; }"
    "QPushButton:hover { background: #2ecc71; }"
    "QPushButton:pressed { background: #1e8449; }";

static const QString BTN_RED =
    "QPushButton { background: #c0392b; color: white; border-color: #a93226; }"
    "QPushButton:hover { background: #e74c3c; }"
    "QPushButton:pressed { background: #96281b; }";

static const QString BTN_BLUE =
    "QPushButton { background: #2980b9; color: white; border-color: #2471a3; }"
    "QPushButton:hover { background: #3498db; }"
    "QPushButton:pressed { background: #1f6391; }";

static const QString BTN_ORANGE =
    "QPushButton { background: #e67e22; color: white; border-color: #cf711a; }"
    "QPushButton:hover { background: #f39c12; }"
    "QPushButton:pressed { background: #ba6617; }";

// ---------------------------------------------------------------------------
// Helper: parse JSON segmentation string into map
// ---------------------------------------------------------------------------
static std::map<std::string, std::string> parseSegmentation(const std::string &text) {
    std::map<std::string, std::string> result;
    if (text.empty()) return result;
    try {
        auto j = json::parse(text);
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) {
                result[it.key()] = it.value().get<std::string>();
            } else {
                result[it.key()] = it.value().dump();
            }
        }
    } catch (...) {
        // silently ignore parse errors
    }
    return result;
}

// ---------------------------------------------------------------------------
// Feedback Widgets — standalone HTTP flow (does not use the C++ SDK)
// ---------------------------------------------------------------------------
// These constants identify this client to the Countly feedback endpoint.
// Server URL / app key / device ID are read from the Init tab at runtime.
static const std::string FW_SDK_VERSION = "1.0.0";
static const std::string FW_SDK_NAME    = "cpp-native";
static const std::string FW_APP_VERSION = "1.0.0";
static const std::string FW_PLATFORM    = "desktop";
static const std::string FW_COMM_URL    = "https://countly_action_event";

struct CountlyFeedbackWidget {
    std::string widgetId;
    std::string type;
    std::string name;
    std::vector<std::string> tags;
    std::string widgetVersion;
};

static size_t fwWriteCallback(void *contents, size_t size, size_t nmemb, std::string *out) {
    size_t totalSize = size * nmemb;
    out->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

static std::string fwHttpGet(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init curl");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error("HTTP failed: " + std::string(curl_easy_strerror(res)));
    if (httpCode != 200)
        throw std::runtime_error("HTTP " + std::to_string(httpCode) + ": " + response);

    return response;
}

static std::string fwUrlEncode(const std::string &value) {
    CURL *curl = curl_easy_init();
    if (!curl) return value;
    char *encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

static std::vector<CountlyFeedbackWidget> fwFetchWidgets(
    const std::string &serverUrl, const std::string &appKey, const std::string &deviceId) {
    std::string url = serverUrl + "/o/sdk"
        "?method=feedback"
        "&app_key=" + fwUrlEncode(appKey) +
        "&device_id=" + fwUrlEncode(deviceId) +
        "&sdk_version=" + fwUrlEncode(FW_SDK_VERSION) +
        "&sdk_name=" + fwUrlEncode(FW_SDK_NAME);

    std::string response = fwHttpGet(url);
    json j = json::parse(response);
    std::vector<CountlyFeedbackWidget> widgets;

    if (!j.contains("result") || !j["result"].is_array()) return widgets;

    for (const auto &w : j["result"]) {
        CountlyFeedbackWidget widget;
        widget.widgetId = w.value("_id", "");
        widget.type = w.value("type", "");
        widget.name = w.value("name", "");
        widget.widgetVersion = w.value("wv", "");
        if (w.contains("tg") && w["tg"].is_array()) {
            for (const auto &tag : w["tg"]) {
                if (tag.is_string()) widget.tags.push_back(tag.get<std::string>());
            }
        }
        widgets.push_back(widget);
    }
    return widgets;
}

static std::string fwConstructWebViewUrl(
    const CountlyFeedbackWidget &widget,
    const std::string &serverUrl, const std::string &appKey, const std::string &deviceId) {
    json custom;
    custom["tc"] = 1;
    if (!widget.widgetVersion.empty()) {
        custom["xb"] = 1;
        custom["rw"] = 1;
    }

    return serverUrl + "/feedback/" + widget.type
        + "?widget_id=" + fwUrlEncode(widget.widgetId)
        + "&device_id=" + fwUrlEncode(deviceId)
        + "&app_key=" + fwUrlEncode(appKey)
        + "&sdk_version=" + fwUrlEncode(FW_SDK_VERSION)
        + "&sdk_name=" + fwUrlEncode(FW_SDK_NAME)
        + "&app_version=" + fwUrlEncode(FW_APP_VERSION)
        + "&platform=" + fwUrlEncode(FW_PLATFORM)
        + "&custom=" + fwUrlEncode(custom.dump());
}

// Custom QWebEnginePage that intercepts Countly widget communication URLs.
class CountlyWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    CountlyFeedbackWidget currentWidget;
    std::function<void()> onWidgetClosed;
    std::function<void(const std::string &)> onLog;

    using QWebEnginePage::QWebEnginePage;

    // Handle target="_blank" links (T&C, privacy policy, etc.)
    QWebEnginePage *createWindow(WebWindowType) override {
        auto *tempPage = new QWebEnginePage(this->profile(), this);
        connect(tempPage, &QWebEnginePage::urlChanged, this, [this, tempPage](const QUrl &url) {
            if (onLog) onLog("[Widgets][NewWindow] " + url.toString().toStdString());
            QDesktopServices::openUrl(url);
            tempPage->deleteLater();
        });
        return tempPage;
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override {
        QString urlStr = QUrl::fromPercentEncoding(url.toEncoded());
        if (onLog) onLog("[Widgets][Intercept] " + urlStr.toStdString());

        // External link interception: cly_x_int=1
        QUrlQuery query(url);
        if (query.hasQueryItem("cly_x_int") && query.queryItemValue("cly_x_int") == "1") {
            if (onLog) onLog("[Widgets]  -> External link, opening in browser");
            QDesktopServices::openUrl(url);
            return false;
        }

        // Communication URL
        if (urlStr.startsWith(QString::fromStdString(FW_COMM_URL))) {
            QUrlQuery commQuery{QUrl{urlStr}};

            // Widget command: close
            if (commQuery.hasQueryItem("cly_widget_command") &&
                commQuery.queryItemValue("cly_widget_command") == "1") {
                if (commQuery.hasQueryItem("close") && commQuery.queryItemValue("close") == "1") {
                    if (onLog) onLog("[Widgets]  -> Widget close command");
                    if (onWidgetClosed) onWidgetClosed();
                }
                return false;
            }

            // Actions
            if (commQuery.hasQueryItem("cly_x_action_event") &&
                commQuery.queryItemValue("cly_x_action_event") == "1") {
                QString action = commQuery.queryItemValue("action");

                if (action == "link") {
                    QString link = commQuery.queryItemValue("link");
                    if (onLog) onLog("[Widgets]  -> Open link in browser: " + link.toStdString());
                    QDesktopServices::openUrl(QUrl(link));
                }

                if (commQuery.hasQueryItem("close") && commQuery.queryItemValue("close") == "1") {
                    if (onLog) onLog("[Widgets]  -> close=1 after action, dismissing");
                    if (onWidgetClosed) onWidgetClosed();
                }
                return false;
            }

            return false; // Block unknown comm URLs
        }

        return true; // Allow normal navigation
    }
};

// Clickable card that previews a single feedback widget in the list.
class WidgetCard : public QFrame {
    Q_OBJECT
public:
    WidgetCard(const CountlyFeedbackWidget &w, QWidget *parent = nullptr)
        : QFrame(parent), widget(w) {
        setFrameShape(QFrame::StyledPanel);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "WidgetCard { background: #ffffff; border: 1px solid #ddd; border-radius: 8px; padding: 12px; }"
            "WidgetCard:hover { border-color: #4A90D9; background: #f0f7ff; }"
        );

        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(4);

        QString typeStr = QString::fromStdString(w.type).toUpper();
        QString badgeColor = "#888";
        if (w.type == "nps") badgeColor = "#E67E22";
        else if (w.type == "survey") badgeColor = "#2ECC71";
        else if (w.type == "rating") badgeColor = "#3498DB";

        auto *typeBadge = new QLabel(typeStr);
        typeBadge->setStyleSheet(QString(
            "background: %1; color: white; border-radius: 4px; padding: 2px 8px; "
            "font-size: 11px; font-weight: bold;"
        ).arg(badgeColor));
        typeBadge->setFixedWidth(typeBadge->sizeHint().width() + 16);

        QString name = QString::fromStdString(w.name);
        if (name.isEmpty()) name = "(unnamed)";
        auto *nameLabel = new QLabel(name);
        nameLabel->setStyleSheet(
            "font-size: 14px; font-weight: bold; color: #333; "
            "border: none; background: transparent;");
        nameLabel->setWordWrap(true);

        QString idStr = QString::fromStdString(w.widgetId);
        if (idStr.length() > 12) idStr = idStr.left(12) + "...";
        auto *idLabel = new QLabel("ID: " + idStr);
        idLabel->setStyleSheet(
            "font-size: 11px; color: #999; border: none; background: transparent;");

        QString verStr = w.widgetVersion.empty()
            ? QString("legacy")
            : "v" + QString::fromStdString(w.widgetVersion);
        auto *verLabel = new QLabel(verStr);
        verLabel->setStyleSheet(
            "font-size: 11px; color: #666; border: none; background: transparent;");

        layout->addWidget(typeBadge);
        layout->addWidget(nameLabel);
        layout->addWidget(idLabel);
        layout->addWidget(verLabel);
    }

    CountlyFeedbackWidget widget;

signals:
    void clicked(const CountlyFeedbackWidget &widget);

protected:
    void mousePressEvent(QMouseEvent *) override {
        emit clicked(widget);
    }
};

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Countly C++ SDK Demo");
        resize(1100, 850);

        auto *centralWidget = new QWidget(this);
        auto *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setContentsMargins(10, 10, 10, 10);
        mainLayout->setSpacing(8);

        // Splitter: top = tabs, bottom = log
        auto *splitter = new QSplitter(Qt::Vertical, centralWidget);

        // Tab widget
        tabs = new QTabWidget();
        buildInitTab();
        buildSessionsTab();
        buildEventsTab();
        buildViewsTab();
        buildCrashesTab();
        buildUserProfileTab();
        buildLocationTab();
        buildDeviceIdTab();
        buildRemoteConfigTab();
        buildFeedbackWidgetsTab();
        splitter->addWidget(tabs);

        // Log panel
        auto *logContainer = new QWidget();
        auto *logLayout = new QVBoxLayout(logContainer);
        logLayout->setContentsMargins(0, 0, 0, 0);
        logLayout->setSpacing(4);

        auto *logHeader = new QHBoxLayout();
        auto *logLabel = new QLabel("SDK Log Output");
        logLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #444;");
        logHeader->addWidget(logLabel);
        logHeader->addStretch();

        auto *clearLogBtn = new QPushButton("Clear Log");
        clearLogBtn->setStyleSheet(BTN_RED);
        clearLogBtn->setFixedWidth(100);
        connect(clearLogBtn, &QPushButton::clicked, this, [this]() { logOutput->clear(); });
        logHeader->addWidget(clearLogBtn);
        logLayout->addLayout(logHeader);

        logOutput = new QPlainTextEdit();
        logOutput->setReadOnly(true);
        logOutput->setFont(QFont("Menlo", 11));
        logOutput->setStyleSheet(
            "QPlainTextEdit { background: #f8f8f8; color: #333; "
            "border: 1px solid #ccc; border-radius: 4px; }");
        logOutput->setMaximumBlockCount(5000);
        logLayout->addWidget(logOutput);
        splitter->addWidget(logContainer);

        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);

        mainLayout->addWidget(splitter);
        setCentralWidget(centralWidget);
    }

signals:
    void logMessageReceived(const QString &message);

public slots:
    void appendLog(const QString &message) {
        logOutput->appendPlainText(message);
        auto cursor = logOutput->textCursor();
        cursor.movePosition(QTextCursor::End);
        logOutput->setTextCursor(cursor);
    }

private:
    // Widgets
    QTabWidget *tabs = nullptr;
    QPlainTextEdit *logOutput = nullptr;

    // Init tab
    QLineEdit *serverUrlEdit = nullptr;
    QLineEdit *appKeyEdit = nullptr;
    QLineEdit *deviceIdEdit = nullptr;
    QLineEdit *dbPathEdit = nullptr;
    QLineEdit *portEdit = nullptr;
    QTextEdit *sbsJsonEdit = nullptr;
    QCheckBox *manualSessionCheck = nullptr;
    QCheckBox *disableSBSCheck = nullptr;
    QCheckBox *alwaysPostCheck = nullptr;
    QCheckBox *enableRemoteConfigCheck = nullptr;
    QCheckBox *disableAutoEventsOnUPCheck = nullptr;
    QLineEdit *saltEdit = nullptr;
    QLineEdit *eqThresholdEdit = nullptr;
    QLineEdit *rqMaxSizeEdit = nullptr;
    QLineEdit *rqBatchSizeEdit = nullptr;
    QLineEdit *sessionIntervalEdit = nullptr;
    QLineEdit *updateIntervalEdit = nullptr;
    QLineEdit *metricsOsEdit = nullptr;
    QLineEdit *metricsOsVersionEdit = nullptr;
    QLineEdit *metricsDeviceEdit = nullptr;
    QLineEdit *metricsResolutionEdit = nullptr;
    QLineEdit *metricsCarrierEdit = nullptr;
    QLineEdit *metricsAppVersionEdit = nullptr;
    QPushButton *initBtn = nullptr;
    QPushButton *stopBtn = nullptr;
    QLabel *sdkStatusLabel = nullptr;

    // Sessions tab
    QLabel *sessionStatusLabel = nullptr;

    // Events tab
    QLineEdit *eventKeyEdit = nullptr;
    QSpinBox *eventCountSpin = nullptr;
    QLineEdit *eventSumEdit = nullptr;
    QLineEdit *eventSegEdit = nullptr;

    // Views tab
    QLineEdit *viewNameEdit = nullptr;
    QLineEdit *viewSegEdit = nullptr;
    QListWidget *activeViewsList = nullptr;

    // Crashes tab
    QLineEdit *crashTitleEdit = nullptr;
    QTextEdit *stackTraceEdit = nullptr;
    QCheckBox *fatalCheck = nullptr;
    QLineEdit *crashOsEdit = nullptr;
    QLineEdit *crashSegEdit = nullptr;
    QLineEdit *breadcrumbEdit = nullptr;

    // User profile tab
    QLineEdit *userNameEdit = nullptr;
    QLineEdit *userUsernameEdit = nullptr;
    QLineEdit *userEmailEdit = nullptr;
    QLineEdit *userPhoneEdit = nullptr;
    QLineEdit *userOrgEdit = nullptr;
    QLineEdit *userPictureEdit = nullptr;
    QLineEdit *userGenderEdit = nullptr;
    QLineEdit *userBirthYearEdit = nullptr;
    QListWidget *customPropsList = nullptr;
    QLineEdit *customKeyEdit = nullptr;
    QLineEdit *customValueEdit = nullptr;

    // Location tab
    QLineEdit *countryCodeEdit = nullptr;
    QLineEdit *cityEdit = nullptr;
    QLineEdit *gpsEdit = nullptr;
    QLineEdit *ipEdit = nullptr;

    // Device ID tab
    QLineEdit *newDeviceIdEdit = nullptr;
    QCheckBox *mergeCheck = nullptr;

    // Remote Config tab
    QLineEdit *rcKeyEdit = nullptr;
    QLabel *rcValueLabel = nullptr;
    QLineEdit *rcKeysForEdit = nullptr;
    QLineEdit *rcKeysExceptEdit = nullptr;

    // Feedback Widgets tab
    QVBoxLayout *fwCardsLayout = nullptr;
    QLabel *fwHeaderLabel = nullptr;
    QStackedWidget *fwRightStack = nullptr;
    QWebEngineView *fwWebView = nullptr;
    CountlyWebPage *fwWebPage = nullptr;
    QTimer *fwLoadTimer = nullptr;

    // State
    bool sdkInitialized = false;
    std::map<std::string, std::string> activeViews; // viewId -> viewName

    // -----------------------------------------------------------------------
    // Helper: create a labeled row
    // -----------------------------------------------------------------------
    QHBoxLayout *labeledRow(const QString &label, QWidget *widget) {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(label);
        lbl->setFixedWidth(130);
        row->addWidget(lbl);
        row->addWidget(widget);
        return row;
    }

    // -----------------------------------------------------------------------
    // Thread-safe log helper
    // -----------------------------------------------------------------------
    void logMsg(const std::string &msg) {
        QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, "appendLog", Qt::QueuedConnection,
                                  Q_ARG(QString, qmsg));
    }

    // -----------------------------------------------------------------------
    // Tab 1: Init and Config
    // -----------------------------------------------------------------------
    void buildInitTab() {
        auto *page = new QWidget();
        auto *scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        // Dev config loader
        auto *devRow = new QHBoxLayout();
        auto *devBtn = new QPushButton("Load Dev Config");
        devBtn->setStyleSheet(BTN_ORANGE);
        devBtn->setToolTip("Loads credentials from dev_config.hpp — edit that file with your test server details");
        connect(devBtn, &QPushButton::clicked, this, &MainWindow::onLoadDevConfig);
        devRow->addWidget(devBtn);
        auto *devHint = new QLabel("Edit dev_config.hpp with your test server credentials");
        devHint->setStyleSheet("color: #888; font-style: italic; font-size: 12px;");
        devRow->addWidget(devHint);
        devRow->addStretch();
        layout->addLayout(devRow);

        // Connection group
        auto *connGroup = new QGroupBox("Connection Settings");
        auto *connLayout = new QVBoxLayout(connGroup);

        serverUrlEdit = new QLineEdit("https://your.server.ly");
        connLayout->addLayout(labeledRow("Server URL:", serverUrlEdit));

        appKeyEdit = new QLineEdit("YOUR_APP_KEY");
        connLayout->addLayout(labeledRow("App Key:", appKeyEdit));

        deviceIdEdit = new QLineEdit("cpp-demo-device");
        connLayout->addLayout(labeledRow("Device ID:", deviceIdEdit));

        dbPathEdit = new QLineEdit("countly_demo.db");
        connLayout->addLayout(labeledRow("Database Path:", dbPathEdit));

        portEdit = new QLineEdit();
        portEdit->setPlaceholderText("Default: 0 (auto from URL)");
        connLayout->addLayout(labeledRow("Port:", portEdit));

        layout->addWidget(connGroup);

        // SBS group
        auto *sbsGroup = new QGroupBox("SDK Behavior Settings (SBS)");
        auto *sbsLayout = new QVBoxLayout(sbsGroup);

        auto *sbsLabel = new QLabel("SBS JSON (pre-init only):");
        sbsLayout->addWidget(sbsLabel);

        sbsJsonEdit = new QTextEdit();
        sbsJsonEdit->setPlaceholderText(
            "Paste SBS JSON here, e.g. {\"key\": \"value\"}");
        sbsJsonEdit->setFixedHeight(100);
        sbsLayout->addWidget(sbsJsonEdit);

        layout->addWidget(sbsGroup);

        // Tuning group
        auto *tuneGroup = new QGroupBox("SDK Tuning");
        auto *tuneLayout = new QVBoxLayout(tuneGroup);

        saltEdit = new QLineEdit();
        saltEdit->setPlaceholderText("Optional — parameter tampering salt");
        tuneLayout->addLayout(labeledRow("Salt:", saltEdit));

        eqThresholdEdit = new QLineEdit();
        eqThresholdEdit->setPlaceholderText("Default: 100 (1–10000)");
        tuneLayout->addLayout(labeledRow("EQ Threshold:", eqThresholdEdit));

        rqMaxSizeEdit = new QLineEdit();
        rqMaxSizeEdit->setPlaceholderText("Default: 1000");
        tuneLayout->addLayout(labeledRow("RQ Max Size:", rqMaxSizeEdit));

        rqBatchSizeEdit = new QLineEdit();
        rqBatchSizeEdit->setPlaceholderText("Default: 100");
        tuneLayout->addLayout(labeledRow("RQ Batch Size:", rqBatchSizeEdit));

        sessionIntervalEdit = new QLineEdit();
        sessionIntervalEdit->setPlaceholderText("Default: 60 (seconds)");
        tuneLayout->addLayout(labeledRow("Session Interval:", sessionIntervalEdit));

        updateIntervalEdit = new QLineEdit();
        updateIntervalEdit->setPlaceholderText("Default: 3000 (milliseconds)");
        tuneLayout->addLayout(labeledRow("Update Loop (ms):", updateIntervalEdit));

        layout->addWidget(tuneGroup);

        // Metrics group
        auto *metricsGroup = new QGroupBox("Device Metrics");
        auto *metricsLayout = new QVBoxLayout(metricsGroup);

        metricsOsEdit = new QLineEdit("macOS");
        metricsLayout->addLayout(labeledRow("OS:", metricsOsEdit));

        metricsOsVersionEdit = new QLineEdit("15.0");
        metricsLayout->addLayout(labeledRow("OS Version:", metricsOsVersionEdit));

        metricsDeviceEdit = new QLineEdit("MacBook");
        metricsLayout->addLayout(labeledRow("Device:", metricsDeviceEdit));

        metricsResolutionEdit = new QLineEdit("2560x1600");
        metricsLayout->addLayout(labeledRow("Resolution:", metricsResolutionEdit));

        metricsCarrierEdit = new QLineEdit();
        metricsCarrierEdit->setPlaceholderText("Optional");
        metricsLayout->addLayout(labeledRow("Carrier:", metricsCarrierEdit));

        metricsAppVersionEdit = new QLineEdit("1.0");
        metricsLayout->addLayout(labeledRow("App Version:", metricsAppVersionEdit));

        layout->addWidget(metricsGroup);

        // Options group
        auto *optGroup = new QGroupBox("Options");
        auto *optLayout = new QVBoxLayout(optGroup);

        manualSessionCheck = new QCheckBox("Manual Session Control");
        optLayout->addWidget(manualSessionCheck);

        disableSBSCheck = new QCheckBox("Disable SBS Updates");
        optLayout->addWidget(disableSBSCheck);

        alwaysPostCheck = new QCheckBox("Always Use POST");
        optLayout->addWidget(alwaysPostCheck);

        enableRemoteConfigCheck = new QCheckBox("Enable Remote Config");
        optLayout->addWidget(enableRemoteConfigCheck);

        disableAutoEventsOnUPCheck = new QCheckBox("Disable Auto Events on User Properties");
        optLayout->addWidget(disableAutoEventsOnUPCheck);

        layout->addWidget(optGroup);

        // Buttons
        auto *btnRow = new QHBoxLayout();

        initBtn = new QPushButton("Initialize SDK");
        initBtn->setStyleSheet(BTN_GREEN);
        connect(initBtn, &QPushButton::clicked, this, &MainWindow::onInitSDK);
        btnRow->addWidget(initBtn);

        stopBtn = new QPushButton("Stop SDK");
        stopBtn->setStyleSheet(BTN_RED);
        stopBtn->setEnabled(false);
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::onStopSDK);
        btnRow->addWidget(stopBtn);

        btnRow->addStretch();
        layout->addLayout(btnRow);

        // Status
        sdkStatusLabel = new QLabel("Status: Not initialized");
        sdkStatusLabel->setStyleSheet(
            "font-weight: bold; color: #888; font-size: 14px; padding: 8px 0;");
        layout->addWidget(sdkStatusLabel);

        layout->addStretch();

        scroll->setWidget(page);
        tabs->addTab(scroll, "Init / Config");
    }

    // -----------------------------------------------------------------------
    // Tab 2: Sessions
    // -----------------------------------------------------------------------
    void buildSessionsTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *group = new QGroupBox("Session Control");
        auto *gLayout = new QVBoxLayout(group);

        auto *infoLabel = new QLabel(
            "Manual session control must be enabled before SDK init to use these.");
        infoLabel->setStyleSheet("color: #888; font-style: italic;");
        infoLabel->setWordWrap(true);
        gLayout->addWidget(infoLabel);

        auto *btnRow = new QHBoxLayout();

        auto *beginBtn = new QPushButton("Begin Session");
        beginBtn->setStyleSheet(BTN_GREEN);
        connect(beginBtn, &QPushButton::clicked, this, &MainWindow::onBeginSession);
        btnRow->addWidget(beginBtn);

        auto *updateBtn = new QPushButton("Update Session");
        updateBtn->setStyleSheet(BTN_BLUE);
        connect(updateBtn, &QPushButton::clicked, this, &MainWindow::onUpdateSession);
        btnRow->addWidget(updateBtn);

        auto *endBtn = new QPushButton("End Session");
        endBtn->setStyleSheet(BTN_RED);
        connect(endBtn, &QPushButton::clicked, this, &MainWindow::onEndSession);
        btnRow->addWidget(endBtn);

        btnRow->addStretch();
        gLayout->addLayout(btnRow);

        sessionStatusLabel = new QLabel("Session: idle");
        sessionStatusLabel->setStyleSheet(
            "font-weight: bold; color: #888; font-size: 14px; padding: 8px 0;");
        gLayout->addWidget(sessionStatusLabel);

        layout->addWidget(group);
        layout->addStretch();
        tabs->addTab(page, "Sessions");
    }

    // -----------------------------------------------------------------------
    // Tab 3: Events
    // -----------------------------------------------------------------------
    void buildEventsTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *group = new QGroupBox("Record Events");
        auto *gLayout = new QVBoxLayout(group);

        eventKeyEdit = new QLineEdit("test_event");
        gLayout->addLayout(labeledRow("Event Key:", eventKeyEdit));

        eventCountSpin = new QSpinBox();
        eventCountSpin->setRange(1, 100000);
        eventCountSpin->setValue(1);
        gLayout->addLayout(labeledRow("Count:", eventCountSpin));

        eventSumEdit = new QLineEdit();
        eventSumEdit->setPlaceholderText("Optional (e.g. 9.99)");
        gLayout->addLayout(labeledRow("Sum:", eventSumEdit));

        eventSegEdit = new QLineEdit();
        eventSegEdit->setPlaceholderText("{\"key\": \"value\"}");
        gLayout->addLayout(labeledRow("Segmentation:", eventSegEdit));

        auto *btnRow = new QHBoxLayout();

        auto *recordBtn = new QPushButton("Record Event");
        recordBtn->setStyleSheet(BTN_GREEN);
        connect(recordBtn, &QPushButton::clicked, this, &MainWindow::onRecordEvent);
        btnRow->addWidget(recordBtn);

        auto *record10Btn = new QPushButton("Record 10 Events");
        record10Btn->setStyleSheet(BTN_BLUE);
        connect(record10Btn, &QPushButton::clicked, this, &MainWindow::onRecord10Events);
        btnRow->addWidget(record10Btn);

        auto *flushBtn = new QPushButton("Flush Events");
        flushBtn->setStyleSheet(BTN_ORANGE);
        connect(flushBtn, &QPushButton::clicked, this, &MainWindow::onFlushEvents);
        btnRow->addWidget(flushBtn);

        btnRow->addStretch();
        gLayout->addLayout(btnRow);

        layout->addWidget(group);

        // Queue info
        auto *queueGroup = new QGroupBox("Queue Status");
        auto *qLayout = new QVBoxLayout(queueGroup);

        auto *checkBtn = new QPushButton("Check Queue Sizes");
        checkBtn->setStyleSheet(BTN_BLUE);
        connect(checkBtn, &QPushButton::clicked, this, [this]() {
            if (!sdkInitialized) {
                logMsg("[App] SDK not initialized");
                return;
            }
            try {
                auto &countly = Countly::getInstance();
                int eq = countly.checkEQSize();
                int rq = countly.checkRQSize();
                logMsg("[App] Event Queue size: " + std::to_string(eq)
                       + ", Request Queue size: " + std::to_string(rq));
            } catch (const std::exception &e) {
                logMsg(std::string("[App][Error] ") + e.what());
            }
        });
        qLayout->addWidget(checkBtn);

        layout->addWidget(queueGroup);
        layout->addStretch();
        tabs->addTab(page, "Events");
    }

    // -----------------------------------------------------------------------
    // Tab 4: Views
    // -----------------------------------------------------------------------
    void buildViewsTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *openGroup = new QGroupBox("Open View");
        auto *oLayout = new QVBoxLayout(openGroup);

        viewNameEdit = new QLineEdit("MainScreen");
        oLayout->addLayout(labeledRow("View Name:", viewNameEdit));

        viewSegEdit = new QLineEdit();
        viewSegEdit->setPlaceholderText("{\"key\": \"value\"}");
        oLayout->addLayout(labeledRow("Segmentation:", viewSegEdit));

        auto *openBtnRow = new QHBoxLayout();

        auto *openBtn = new QPushButton("Open View");
        openBtn->setStyleSheet(BTN_GREEN);
        connect(openBtn, &QPushButton::clicked, this, &MainWindow::onOpenView);
        openBtnRow->addWidget(openBtn);

        auto *closeByNameBtn = new QPushButton("Close View by Name");
        closeByNameBtn->setStyleSheet(BTN_RED);
        connect(closeByNameBtn, &QPushButton::clicked,
                this, &MainWindow::onCloseViewByName);
        openBtnRow->addWidget(closeByNameBtn);

        openBtnRow->addStretch();
        oLayout->addLayout(openBtnRow);
        layout->addWidget(openGroup);

        // Active views
        auto *activeGroup = new QGroupBox("Active Views");
        auto *aLayout = new QVBoxLayout(activeGroup);

        activeViewsList = new QListWidget();
        activeViewsList->setMinimumHeight(120);
        aLayout->addWidget(activeViewsList);

        auto *closeSelectedBtn = new QPushButton("Close Selected View");
        closeSelectedBtn->setStyleSheet(BTN_RED);
        connect(closeSelectedBtn, &QPushButton::clicked,
                this, &MainWindow::onCloseSelectedView);
        aLayout->addWidget(closeSelectedBtn);

        layout->addWidget(activeGroup);
        layout->addStretch();
        tabs->addTab(page, "Views");
    }

    // -----------------------------------------------------------------------
    // Tab 5: Crashes
    // -----------------------------------------------------------------------
    void buildCrashesTab() {
        auto *page = new QWidget();
        auto *scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        // Breadcrumbs
        auto *bcGroup = new QGroupBox("Breadcrumbs");
        auto *bcLayout = new QVBoxLayout(bcGroup);

        breadcrumbEdit = new QLineEdit();
        breadcrumbEdit->setPlaceholderText("Enter a breadcrumb message");
        bcLayout->addLayout(labeledRow("Breadcrumb:", breadcrumbEdit));

        auto *addBcBtn = new QPushButton("Add Breadcrumb");
        addBcBtn->setStyleSheet(BTN_BLUE);
        connect(addBcBtn, &QPushButton::clicked, this, &MainWindow::onAddBreadcrumb);
        bcLayout->addWidget(addBcBtn);
        layout->addWidget(bcGroup);

        // Exception
        auto *exGroup = new QGroupBox("Record Exception");
        auto *exLayout = new QVBoxLayout(exGroup);

        crashTitleEdit = new QLineEdit("NullPointerException");
        exLayout->addLayout(labeledRow("Crash Title:", crashTitleEdit));

        auto *stLabel = new QLabel("Stack Trace:");
        exLayout->addWidget(stLabel);
        stackTraceEdit = new QTextEdit();
        stackTraceEdit->setPlaceholderText(
            "com.example.app.Main.run(Main.java:42)\n"
            "com.example.app.App.start(App.java:10)");
        stackTraceEdit->setFixedHeight(100);
        exLayout->addWidget(stackTraceEdit);

        fatalCheck = new QCheckBox("Fatal");
        fatalCheck->setChecked(false);
        exLayout->addWidget(fatalCheck);

        crashOsEdit = new QLineEdit("macOS 15.0");
        exLayout->addLayout(labeledRow("OS Metric:", crashOsEdit));

        crashSegEdit = new QLineEdit();
        crashSegEdit->setPlaceholderText("{\"module\": \"network\"}");
        exLayout->addLayout(labeledRow("Segmentation:", crashSegEdit));

        auto *recordExBtn = new QPushButton("Record Exception");
        recordExBtn->setStyleSheet(BTN_RED);
        connect(recordExBtn, &QPushButton::clicked,
                this, &MainWindow::onRecordException);
        exLayout->addWidget(recordExBtn);

        layout->addWidget(exGroup);
        layout->addStretch();

        scroll->setWidget(page);
        tabs->addTab(scroll, "Crashes");
    }

    // -----------------------------------------------------------------------
    // Tab 6: User Profile
    // -----------------------------------------------------------------------
    void buildUserProfileTab() {
        auto *page = new QWidget();
        auto *scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        // Named properties
        auto *namedGroup = new QGroupBox("Named User Properties");
        auto *nLayout = new QVBoxLayout(namedGroup);

        userNameEdit = new QLineEdit();
        userNameEdit->setPlaceholderText("John Doe");
        nLayout->addLayout(labeledRow("Name:", userNameEdit));

        userUsernameEdit = new QLineEdit();
        userUsernameEdit->setPlaceholderText("johndoe");
        nLayout->addLayout(labeledRow("Username:", userUsernameEdit));

        userEmailEdit = new QLineEdit();
        userEmailEdit->setPlaceholderText("john@example.com");
        nLayout->addLayout(labeledRow("Email:", userEmailEdit));

        userPhoneEdit = new QLineEdit();
        userPhoneEdit->setPlaceholderText("+1234567890");
        nLayout->addLayout(labeledRow("Phone:", userPhoneEdit));

        userOrgEdit = new QLineEdit();
        userOrgEdit->setPlaceholderText("Acme Corp");
        nLayout->addLayout(labeledRow("Organization:", userOrgEdit));

        userPictureEdit = new QLineEdit();
        userPictureEdit->setPlaceholderText("https://example.com/pic.jpg");
        nLayout->addLayout(labeledRow("Picture URL:", userPictureEdit));

        userGenderEdit = new QLineEdit();
        userGenderEdit->setPlaceholderText("M or F");
        nLayout->addLayout(labeledRow("Gender:", userGenderEdit));

        userBirthYearEdit = new QLineEdit();
        userBirthYearEdit->setPlaceholderText("1990");
        nLayout->addLayout(labeledRow("Birth Year:", userBirthYearEdit));

        auto *setUserBtn = new QPushButton("Set User Details");
        setUserBtn->setStyleSheet(BTN_GREEN);
        connect(setUserBtn, &QPushButton::clicked,
                this, &MainWindow::onSetUserDetails);
        nLayout->addWidget(setUserBtn);

        layout->addWidget(namedGroup);

        // Custom properties
        auto *customGroup = new QGroupBox("Custom User Properties");
        auto *cLayout = new QVBoxLayout(customGroup);

        customPropsList = new QListWidget();
        customPropsList->setMinimumHeight(80);
        cLayout->addWidget(customPropsList);

        auto *addRow = new QHBoxLayout();
        customKeyEdit = new QLineEdit();
        customKeyEdit->setPlaceholderText("Key");
        addRow->addWidget(customKeyEdit);
        customValueEdit = new QLineEdit();
        customValueEdit->setPlaceholderText("Value");
        addRow->addWidget(customValueEdit);

        auto *addPropBtn = new QPushButton("Add");
        addPropBtn->setStyleSheet(BTN_BLUE);
        addPropBtn->setFixedWidth(60);
        connect(addPropBtn, &QPushButton::clicked, this, [this]() {
            QString key = customKeyEdit->text().trimmed();
            QString val = customValueEdit->text().trimmed();
            if (key.isEmpty()) return;
            customPropsList->addItem(key + " = " + val);
            customKeyEdit->clear();
            customValueEdit->clear();
        });
        addRow->addWidget(addPropBtn);

        auto *removePropBtn = new QPushButton("Remove");
        removePropBtn->setStyleSheet(BTN_RED);
        removePropBtn->setFixedWidth(80);
        connect(removePropBtn, &QPushButton::clicked, this, [this]() {
            auto *item = customPropsList->currentItem();
            if (item) delete item;
        });
        addRow->addWidget(removePropBtn);

        cLayout->addLayout(addRow);

        auto *setCustomBtn = new QPushButton("Set Custom User Details");
        setCustomBtn->setStyleSheet(BTN_GREEN);
        connect(setCustomBtn, &QPushButton::clicked,
                this, &MainWindow::onSetCustomUserDetails);
        cLayout->addWidget(setCustomBtn);

        layout->addWidget(customGroup);
        layout->addStretch();

        scroll->setWidget(page);
        tabs->addTab(scroll, "User Profile");
    }

    // -----------------------------------------------------------------------
    // Tab 7: Location
    // -----------------------------------------------------------------------
    void buildLocationTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *group = new QGroupBox("Set Location");
        auto *gLayout = new QVBoxLayout(group);

        countryCodeEdit = new QLineEdit();
        countryCodeEdit->setPlaceholderText("US");
        gLayout->addLayout(labeledRow("Country Code:", countryCodeEdit));

        cityEdit = new QLineEdit();
        cityEdit->setPlaceholderText("New York");
        gLayout->addLayout(labeledRow("City:", cityEdit));

        gpsEdit = new QLineEdit();
        gpsEdit->setPlaceholderText("40.7128,-74.0060");
        gLayout->addLayout(labeledRow("GPS Coordinates:", gpsEdit));

        ipEdit = new QLineEdit();
        ipEdit->setPlaceholderText("192.168.1.1");
        gLayout->addLayout(labeledRow("IP Address:", ipEdit));

        auto *setLocBtn = new QPushButton("Set Location");
        setLocBtn->setStyleSheet(BTN_GREEN);
        connect(setLocBtn, &QPushButton::clicked, this, &MainWindow::onSetLocation);
        gLayout->addWidget(setLocBtn);

        layout->addWidget(group);
        layout->addStretch();
        tabs->addTab(page, "Location");
    }

    // -----------------------------------------------------------------------
    // Tab 8: Device ID
    // -----------------------------------------------------------------------
    void buildDeviceIdTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *group = new QGroupBox("Change Device ID");
        auto *gLayout = new QVBoxLayout(group);

        newDeviceIdEdit = new QLineEdit();
        newDeviceIdEdit->setPlaceholderText("new-device-id");
        gLayout->addLayout(labeledRow("New Device ID:", newDeviceIdEdit));

        mergeCheck = new QCheckBox("Merge (same_user = true)");
        gLayout->addWidget(mergeCheck);

        auto *changeBtn = new QPushButton("Change Device ID");
        changeBtn->setStyleSheet(BTN_ORANGE);
        connect(changeBtn, &QPushButton::clicked,
                this, &MainWindow::onChangeDeviceId);
        gLayout->addWidget(changeBtn);

        layout->addWidget(group);
        layout->addStretch();
        tabs->addTab(page, "Device ID");
    }

    // -----------------------------------------------------------------------
    // Tab 9: Remote Config
    // -----------------------------------------------------------------------
    void buildRemoteConfigTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 16);

        auto *infoLabel = new QLabel(
            "Remote Config must be enabled in Init / Config before SDK initialization.");
        infoLabel->setStyleSheet("color: #888; font-style: italic;");
        infoLabel->setWordWrap(true);
        layout->addWidget(infoLabel);

        // Fetch all
        auto *fetchGroup = new QGroupBox("Fetch Remote Config");
        auto *fetchLayout = new QVBoxLayout(fetchGroup);

        auto *fetchAllBtn = new QPushButton("Update Remote Config (all keys)");
        fetchAllBtn->setStyleSheet(BTN_BLUE);
        connect(fetchAllBtn, &QPushButton::clicked, this, [this]() {
            if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
            logMsg("[App] Fetching remote config (all keys)...");
            try {
                Countly::getInstance().updateRemoteConfig();
                logMsg("[App] Remote config update requested.");
            } catch (const std::exception &e) {
                logMsg(std::string("[App][Error] ") + e.what());
            }
        });
        fetchLayout->addWidget(fetchAllBtn);

        // Fetch for specific keys
        rcKeysForEdit = new QLineEdit();
        rcKeysForEdit->setPlaceholderText("Comma-separated keys, e.g. color,timeout");
        fetchLayout->addLayout(labeledRow("Keys (include):", rcKeysForEdit));

        auto *fetchForBtn = new QPushButton("Update Remote Config (specific keys)");
        fetchForBtn->setStyleSheet(BTN_BLUE);
        connect(fetchForBtn, &QPushButton::clicked, this, [this]() {
            if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
            std::string keysStr = rcKeysForEdit->text().trimmed().toStdString();
            if (keysStr.empty()) { logMsg("[App] Enter at least one key."); return; }
            std::vector<std::string> keys;
            std::istringstream ss(keysStr);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(' '));
                token.erase(token.find_last_not_of(' ') + 1);
                if (!token.empty()) keys.push_back(token);
            }
            logMsg("[App] Fetching remote config for " + std::to_string(keys.size()) + " keys...");
            try {
                Countly::getInstance().updateRemoteConfigFor(keys.data(), keys.size());
                logMsg("[App] Remote config update (for keys) requested.");
            } catch (const std::exception &e) {
                logMsg(std::string("[App][Error] ") + e.what());
            }
        });
        fetchLayout->addWidget(fetchForBtn);

        // Fetch except specific keys
        rcKeysExceptEdit = new QLineEdit();
        rcKeysExceptEdit->setPlaceholderText("Comma-separated keys to exclude");
        fetchLayout->addLayout(labeledRow("Keys (exclude):", rcKeysExceptEdit));

        auto *fetchExceptBtn = new QPushButton("Update Remote Config (except keys)");
        fetchExceptBtn->setStyleSheet(BTN_BLUE);
        connect(fetchExceptBtn, &QPushButton::clicked, this, [this]() {
            if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
            std::string keysStr = rcKeysExceptEdit->text().trimmed().toStdString();
            if (keysStr.empty()) { logMsg("[App] Enter at least one key."); return; }
            std::vector<std::string> keys;
            std::istringstream ss(keysStr);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(' '));
                token.erase(token.find_last_not_of(' ') + 1);
                if (!token.empty()) keys.push_back(token);
            }
            logMsg("[App] Fetching remote config except " + std::to_string(keys.size()) + " keys...");
            try {
                Countly::getInstance().updateRemoteConfigExcept(keys.data(), keys.size());
                logMsg("[App] Remote config update (except keys) requested.");
            } catch (const std::exception &e) {
                logMsg(std::string("[App][Error] ") + e.what());
            }
        });
        fetchLayout->addWidget(fetchExceptBtn);

        layout->addWidget(fetchGroup);

        // Get value
        auto *getGroup = new QGroupBox("Get Remote Config Value");
        auto *getLayout = new QVBoxLayout(getGroup);

        rcKeyEdit = new QLineEdit();
        rcKeyEdit->setPlaceholderText("Key name, e.g. color");
        getLayout->addLayout(labeledRow("Key:", rcKeyEdit));

        auto *getBtn = new QPushButton("Get Value");
        getBtn->setStyleSheet(BTN_GREEN);
        connect(getBtn, &QPushButton::clicked, this, [this]() {
            if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
            std::string key = rcKeyEdit->text().trimmed().toStdString();
            if (key.empty()) { logMsg("[App] Enter a key."); return; }
            try {
                auto val = Countly::getInstance().getRemoteConfigValue(key);
                std::string valStr = val.dump();
                rcValueLabel->setText("Value: " + QString::fromStdString(valStr));
                logMsg("[App] Remote config [" + key + "] = " + valStr);
            } catch (const std::exception &e) {
                logMsg(std::string("[App][Error] ") + e.what());
            }
        });
        getLayout->addWidget(getBtn);

        rcValueLabel = new QLabel("Value: (none)");
        rcValueLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 8px 0; color: #2c3e50;");
        rcValueLabel->setWordWrap(true);
        rcValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        getLayout->addWidget(rcValueLabel);

        layout->addWidget(getGroup);
        layout->addStretch();
        tabs->addTab(page, "Remote Config");
    }

    // -----------------------------------------------------------------------
    // Tab 10: Feedback Widgets
    // -----------------------------------------------------------------------
    // Standalone HTTP flow — does not go through the C++ SDK. Reads server
    // URL / app key / device ID from the Init tab.
    void buildFeedbackWidgetsTab() {
        auto *page = new QWidget();
        auto *layout = new QVBoxLayout(page);
        layout->setSpacing(8);
        layout->setContentsMargins(10, 10, 10, 10);

        auto *infoLabel = new QLabel(
            "Fetches feedback widgets directly via HTTP (no SDK call). "
            "Uses the Server URL, App Key, and Device ID from the Init tab.");
        infoLabel->setStyleSheet("color: #888; font-style: italic;");
        infoLabel->setWordWrap(true);
        layout->addWidget(infoLabel);

        auto *fetchBtn = new QPushButton("Fetch Widgets");
        fetchBtn->setStyleSheet(BTN_BLUE);
        fetchBtn->setFixedWidth(160);
        connect(fetchBtn, &QPushButton::clicked, this, &MainWindow::onFetchFeedbackWidgets);
        layout->addWidget(fetchBtn);

        auto *splitter = new QSplitter(Qt::Horizontal, page);

        // Left: card list (populated on fetch)
        auto *scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setMinimumWidth(280);
        scrollArea->setMaximumWidth(380);
        scrollArea->setStyleSheet("QScrollArea { background: #f5f5f5; border: 1px solid #ddd; border-radius: 4px; }");

        auto *cardContainer = new QWidget();
        fwCardsLayout = new QVBoxLayout(cardContainer);
        fwCardsLayout->setSpacing(8);
        fwCardsLayout->setContentsMargins(10, 10, 10, 10);

        fwHeaderLabel = new QLabel("Widgets (0)");
        fwHeaderLabel->setStyleSheet(
            "font-size: 14px; font-weight: bold; color: #333; padding: 4px 0;");
        fwCardsLayout->addWidget(fwHeaderLabel);
        fwCardsLayout->addStretch();

        scrollArea->setWidget(cardContainer);
        splitter->addWidget(scrollArea);

        // Right: placeholder or web view
        fwRightStack = new QStackedWidget();

        auto *placeholder = new QLabel("Click a widget on the left to present it here.");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("font-size: 14px; color: #999; background: #fafafa;");
        fwRightStack->addWidget(placeholder);

        fwWebView = new QWebEngineView();
        fwWebPage = new CountlyWebPage(fwWebView->page()->profile(), fwWebView);
        fwWebPage->onLog = [this](const std::string &msg) { logMsg(msg); };
        fwWebPage->onWidgetClosed = [this]() {
            QTimer::singleShot(0, this, [this]() {
                fwWebView->setUrl(QUrl("about:blank"));
                fwRightStack->setCurrentIndex(0);
                logMsg("[Widgets] Widget closed, back to card list");
            });
        };
        fwWebView->setPage(fwWebPage);
        fwWebView->setVisible(false);
        fwRightStack->addWidget(fwWebView);

        splitter->addWidget(fwRightStack);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);

        layout->addWidget(splitter, 1);

        fwLoadTimer = new QTimer(this);
        fwLoadTimer->setSingleShot(true);
        fwLoadTimer->setInterval(60000);
        connect(fwLoadTimer, &QTimer::timeout, this, [this]() {
            logMsg("[Widgets] Page load exceeded 60s, closing");
            fwWebView->setUrl(QUrl("about:blank"));
            fwRightStack->setCurrentIndex(0);
        });

        connect(fwWebView, &QWebEngineView::loadFinished, this, [this](bool ok) {
            fwLoadTimer->stop();
            if (ok) {
                logMsg("[Widgets] Page loaded");
                fwWebView->setVisible(true);
                fwRightStack->setCurrentIndex(1);
            } else {
                logMsg("[Widgets] Page load failed");
                fwRightStack->setCurrentIndex(0);
            }
        });

        tabs->addTab(page, "Feedback Widgets");
    }

    // -----------------------------------------------------------------------
    // SDK Actions
    // -----------------------------------------------------------------------

    void onLoadDevConfig() {
        DevConfig dc;
        serverUrlEdit->setText(QString::fromStdString(dc.serverUrl));
        appKeyEdit->setText(QString::fromStdString(dc.appKey));
        deviceIdEdit->setText(QString::fromStdString(dc.deviceId));
        dbPathEdit->setText(QString::fromStdString(dc.dbPath));
        portEdit->setText(QString::fromStdString(dc.port));
        saltEdit->setText(QString::fromStdString(dc.salt));
        eqThresholdEdit->setText(QString::fromStdString(dc.eqThreshold));
        rqMaxSizeEdit->setText(QString::fromStdString(dc.rqMaxSize));
        rqBatchSizeEdit->setText(QString::fromStdString(dc.rqBatchSize));
        sessionIntervalEdit->setText(QString::fromStdString(dc.sessionInterval));
        updateIntervalEdit->setText(QString::fromStdString(dc.updateInterval));
        metricsOsEdit->setText(QString::fromStdString(dc.metricsOs));
        metricsOsVersionEdit->setText(QString::fromStdString(dc.metricsOsVersion));
        metricsDeviceEdit->setText(QString::fromStdString(dc.metricsDevice));
        metricsResolutionEdit->setText(QString::fromStdString(dc.metricsResolution));
        metricsCarrierEdit->setText(QString::fromStdString(dc.metricsCarrier));
        metricsAppVersionEdit->setText(QString::fromStdString(dc.metricsAppVersion));
        sbsJsonEdit->setText(QString::fromStdString(dc.sbsJson));
        manualSessionCheck->setChecked(dc.manualSession);
        disableSBSCheck->setChecked(dc.disableSBSUpdates);
        alwaysPostCheck->setChecked(dc.alwaysPost);
        enableRemoteConfigCheck->setChecked(dc.enableRemoteConfig);
        disableAutoEventsOnUPCheck->setChecked(dc.disableAutoEventsOnUP);
        logMsg("[App] Dev config loaded.");
    }

    void onInitSDK() {
        if (sdkInitialized) {
            logMsg("[App] SDK is already initialized.");
            return;
        }

        std::string serverUrl = serverUrlEdit->text().trimmed().toStdString();
        std::string appKey = appKeyEdit->text().trimmed().toStdString();
        std::string deviceId = deviceIdEdit->text().trimmed().toStdString();
        std::string dbPath = dbPathEdit->text().trimmed().toStdString();
        std::string sbsJson = sbsJsonEdit->toPlainText().trimmed().toStdString();
        bool manualSession = manualSessionCheck->isChecked();
        bool disableSBS = disableSBSCheck->isChecked();
        bool usePost = alwaysPostCheck->isChecked();

        if (serverUrl.empty() || appKey.empty()) {
            logMsg("[App] Server URL and App Key are required.");
            return;
        }

        logMsg("[App] Initializing SDK...");
        logMsg("[App]   Server URL: " + serverUrl);
        logMsg("[App]   App Key: " + appKey);
        logMsg("[App]   Device ID: " + deviceId);

        try {
            auto &countly = Countly::getInstance();

            // Set logger to forward to GUI (thread-safe via logMsg)
            countly.setLogger([](LogLevel level, const std::string &message) {
                std::string prefix;
                switch (level) {
                    case LogLevel::DEBUG:   prefix = "[DEBUG] "; break;
                    case LogLevel::INFO:    prefix = "[INFO]  "; break;
                    case LogLevel::WARNING: prefix = "[WARN]  "; break;
                    case LogLevel::ERROR:   prefix = "[ERROR] "; break;
                    case LogLevel::FATAL:   prefix = "[FATAL] "; break;
                }
                std::string fullMsg = prefix + message;
                std::cout << fullMsg << std::endl;
                if (sInstance) {
                    sInstance->logMsg(fullMsg);
                }
            });

            if (!deviceId.empty()) {
                countly.setDeviceID(deviceId);
            }

            countly.SetPath(dbPath);

            if (manualSession) {
                countly.enableManualSessionControl();
                logMsg("[App]   Manual session control: enabled");
            }

            if (disableSBS) {
                countly.disableSDKBehaviorSettingsUpdates();
                logMsg("[App]   SBS updates: disabled");
            }

            if (!sbsJson.empty()) {
                countly.setSDKBehaviorSettings(sbsJson);
                logMsg("[App]   SBS JSON provided: " + sbsJson);
            }

            if (usePost) {
                countly.alwaysUsePost(true);
                logMsg("[App]   Always POST: enabled");
            }

            if (enableRemoteConfigCheck->isChecked()) {
                countly.enableRemoteConfig();
                logMsg("[App]   Remote config: enabled");
            }

            if (disableAutoEventsOnUPCheck->isChecked()) {
                countly.disableAutoEventsOnUserProperties();
                logMsg("[App]   Auto events on user properties: disabled");
            }

            // Salt
            std::string salt = saltEdit->text().trimmed().toStdString();
            if (!salt.empty()) {
                countly.setSalt(salt);
                logMsg("[App]   Salt: " + salt);
            }

            // EQ threshold
            std::string eqStr = eqThresholdEdit->text().trimmed().toStdString();
            if (!eqStr.empty()) {
                int eqVal = std::stoi(eqStr);
                countly.setEventsToRQThreshold(eqVal);
                logMsg("[App]   EQ threshold: " + eqStr);
            }

            // RQ max size
            std::string rqStr = rqMaxSizeEdit->text().trimmed().toStdString();
            if (!rqStr.empty()) {
                unsigned int rqVal = std::stoul(rqStr);
                countly.setMaxRequestQueueSize(rqVal);
                logMsg("[App]   RQ max size: " + rqStr);
            }

            // RQ batch size
            std::string batchStr = rqBatchSizeEdit->text().trimmed().toStdString();
            if (!batchStr.empty()) {
                unsigned int batchVal = std::stoul(batchStr);
                countly.setMaxRQProcessingBatchSize(batchVal);
                logMsg("[App]   RQ batch size: " + batchStr);
            }

            // Session update interval
            std::string suiStr = sessionIntervalEdit->text().trimmed().toStdString();
            if (!suiStr.empty()) {
                unsigned short suiVal = static_cast<unsigned short>(std::stoul(suiStr));
                countly.setAutomaticSessionUpdateInterval(suiVal);
                logMsg("[App]   Session update interval: " + suiStr + "s");
            }

            // Metrics
            countly.setMetrics(
                metricsOsEdit->text().trimmed().toStdString(),
                metricsOsVersionEdit->text().trimmed().toStdString(),
                metricsDeviceEdit->text().trimmed().toStdString(),
                metricsResolutionEdit->text().trimmed().toStdString(),
                metricsCarrierEdit->text().trimmed().toStdString(),
                metricsAppVersionEdit->text().trimmed().toStdString()
            );

            // Port
            int port = 0;
            std::string portStr = portEdit->text().trimmed().toStdString();
            if (!portStr.empty()) {
                port = std::stoi(portStr);
                logMsg("[App]   Port: " + portStr);
            }

            countly.start(appKey, serverUrl, port, true);

            // Update loop interval (post-init is OK for this one)
            std::string uiStr = updateIntervalEdit->text().trimmed().toStdString();
            if (!uiStr.empty()) {
                size_t uiVal = std::stoul(uiStr);
                countly.setUpdateInterval(uiVal);
                logMsg("[App]   Update loop interval: " + uiStr + "ms");
            }

            sdkInitialized = true;
            initBtn->setEnabled(false);
            stopBtn->setEnabled(true);
            sdkStatusLabel->setText("Status: Initialized");
            sdkStatusLabel->setStyleSheet(
                "font-weight: bold; color: #27ae60; font-size: 14px; padding: 8px 0;");
            logMsg("[App] SDK initialized successfully.");

        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] SDK init failed: ") + e.what());
        }
    }

    void onStopSDK() {
        if (!sdkInitialized) {
            logMsg("[App] SDK is not initialized.");
            return;
        }

        logMsg("[App] Stopping SDK...");
        try {
            Countly::getInstance().stop();
            sdkInitialized = false;
            initBtn->setEnabled(true);
            stopBtn->setEnabled(false);
            sdkStatusLabel->setText("Status: Stopped");
            sdkStatusLabel->setStyleSheet(
                "font-weight: bold; color: #c0392b; font-size: 14px; padding: 8px 0;");
            activeViews.clear();
            refreshActiveViewsList();
            logMsg("[App] SDK stopped.");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] SDK stop failed: ") + e.what());
        }
    }

    void onBeginSession() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        logMsg("[App] Beginning session...");
        try {
            bool result = Countly::getInstance().beginSession();
            sessionStatusLabel->setText(
                result ? "Session: active" : "Session: begin failed");
            sessionStatusLabel->setStyleSheet(
                result
                    ? "font-weight: bold; color: #27ae60; font-size: 14px; padding: 8px 0;"
                    : "font-weight: bold; color: #c0392b; font-size: 14px; padding: 8px 0;");
            logMsg("[App] beginSession() returned "
                   + std::string(result ? "true" : "false"));
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onUpdateSession() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        logMsg("[App] Updating session...");
        try {
            bool result = Countly::getInstance().updateSession();
            logMsg("[App] updateSession() returned "
                   + std::string(result ? "true" : "false"));
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onEndSession() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        logMsg("[App] Ending session...");
        try {
            bool result = Countly::getInstance().endSession();
            sessionStatusLabel->setText("Session: ended");
            sessionStatusLabel->setStyleSheet(
                "font-weight: bold; color: #888; font-size: 14px; padding: 8px 0;");
            logMsg("[App] endSession() returned "
                   + std::string(result ? "true" : "false"));
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void recordSingleEvent() {
        std::string key = eventKeyEdit->text().trimmed().toStdString();
        int count = eventCountSpin->value();
        std::string sumStr = eventSumEdit->text().trimmed().toStdString();
        std::string segStr = eventSegEdit->text().trimmed().toStdString();

        if (key.empty()) { logMsg("[App] Event key is required."); return; }

        try {
            auto &countly = Countly::getInstance();
            auto segMap = parseSegmentation(segStr);

            if (!sumStr.empty()) {
                double sum = std::stod(sumStr);
                cly::Event event(key, count, sum);
                for (const auto &kv : segMap) {
                    event.addSegmentation(kv.first, kv.second);
                }
                countly.addEvent(event);
                logMsg("[App] Recorded event: " + key
                       + " (count=" + std::to_string(count)
                       + ", sum=" + sumStr + ")");
            } else {
                cly::Event event(key, count);
                for (const auto &kv : segMap) {
                    event.addSegmentation(kv.first, kv.second);
                }
                countly.addEvent(event);
                logMsg("[App] Recorded event: " + key
                       + " (count=" + std::to_string(count) + ")");
            }
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onRecordEvent() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        recordSingleEvent();
    }

    void onRecord10Events() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        logMsg("[App] Recording 10 events...");
        for (int i = 0; i < 10; ++i) {
            recordSingleEvent();
        }
        logMsg("[App] 10 events recorded.");
    }

    void onFlushEvents() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        logMsg("[App] Flushing events...");
        try {
            Countly::getInstance().flushEvents();
            logMsg("[App] Events flushed.");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onOpenView() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        std::string name = viewNameEdit->text().trimmed().toStdString();
        std::string segStr = viewSegEdit->text().trimmed().toStdString();

        if (name.empty()) { logMsg("[App] View name is required."); return; }

        try {
            auto segMap = parseSegmentation(segStr);
            std::string viewId =
                Countly::getInstance().views().openView(name, segMap);
            activeViews[viewId] = name;
            refreshActiveViewsList();
            logMsg("[App] Opened view: \"" + name + "\" (id: " + viewId + ")");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onCloseViewByName() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        std::string name = viewNameEdit->text().trimmed().toStdString();
        if (name.empty()) { logMsg("[App] View name is required."); return; }

        try {
            Countly::getInstance().views().closeViewWithName(name);
            // Remove from local tracking
            for (auto it = activeViews.begin(); it != activeViews.end(); ) {
                if (it->second == name) {
                    it = activeViews.erase(it);
                } else {
                    ++it;
                }
            }
            refreshActiveViewsList();
            logMsg("[App] Closed view by name: \"" + name + "\"");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onCloseSelectedView() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        auto *item = activeViewsList->currentItem();
        if (!item) { logMsg("[App] No view selected."); return; }

        std::string viewId = item->data(Qt::UserRole).toString().toStdString();
        try {
            Countly::getInstance().views().closeViewWithID(viewId);
            activeViews.erase(viewId);
            refreshActiveViewsList();
            logMsg("[App] Closed view by ID: " + viewId);
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void refreshActiveViewsList() {
        activeViewsList->clear();
        for (const auto &kv : activeViews) {
            auto *item = new QListWidgetItem(
                QString::fromStdString(kv.second)
                + "  [" + QString::fromStdString(kv.first) + "]");
            item->setData(Qt::UserRole, QString::fromStdString(kv.first));
            activeViewsList->addItem(item);
        }
    }

    void onAddBreadcrumb() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }
        std::string bc = breadcrumbEdit->text().trimmed().toStdString();
        if (bc.empty()) { logMsg("[App] Breadcrumb text is required."); return; }

        try {
            Countly::getInstance().crash().addBreadcrumb(bc);
            logMsg("[App] Added breadcrumb: \"" + bc + "\"");
            breadcrumbEdit->clear();
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onRecordException() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }

        std::string title = crashTitleEdit->text().trimmed().toStdString();
        std::string stackTrace =
            stackTraceEdit->toPlainText().trimmed().toStdString();
        bool fatal = fatalCheck->isChecked();
        std::string os = crashOsEdit->text().trimmed().toStdString();
        std::string segStr = crashSegEdit->text().trimmed().toStdString();

        if (title.empty()) { logMsg("[App] Crash title is required."); return; }

        try {
            std::map<std::string, std::string> crashMetrics;
            crashMetrics["_os"] = os.empty() ? "macOS" : os;
            crashMetrics["_app_version"] = "1.0";
            crashMetrics["_error"] = title;

            auto segMap = parseSegmentation(segStr);

            Countly::getInstance().crash().recordException(
                title, stackTrace, fatal, crashMetrics, segMap);
            logMsg("[App] Recorded exception: \"" + title
                   + "\" (fatal=" + (fatal ? "true" : "false") + ")");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onSetUserDetails() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }

        std::map<std::string, std::string> details;
        auto addIfNotEmpty = [&](const std::string &key, QLineEdit *edit) {
            std::string val = edit->text().trimmed().toStdString();
            if (!val.empty()) details[key] = val;
        };

        addIfNotEmpty("name", userNameEdit);
        addIfNotEmpty("username", userUsernameEdit);
        addIfNotEmpty("email", userEmailEdit);
        addIfNotEmpty("phone", userPhoneEdit);
        addIfNotEmpty("organization", userOrgEdit);
        addIfNotEmpty("picture", userPictureEdit);
        addIfNotEmpty("gender", userGenderEdit);
        addIfNotEmpty("byear", userBirthYearEdit);

        if (details.empty()) {
            logMsg("[App] No user details to set.");
            return;
        }

        try {
            Countly::getInstance().setUserDetails(details);
            logMsg("[App] User details set ("
                   + std::to_string(details.size()) + " properties).");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onSetCustomUserDetails() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }

        std::map<std::string, std::string> customDetails;
        for (int i = 0; i < customPropsList->count(); ++i) {
            QString text = customPropsList->item(i)->text();
            int eqIdx = text.indexOf(" = ");
            if (eqIdx > 0) {
                QString key = text.left(eqIdx);
                QString val = text.mid(eqIdx + 3);
                customDetails[key.toStdString()] = val.toStdString();
            }
        }

        if (customDetails.empty()) {
            logMsg("[App] No custom properties to set.");
            return;
        }

        try {
            Countly::getInstance().setCustomUserDetails(customDetails);
            logMsg("[App] Custom user details set ("
                   + std::to_string(customDetails.size()) + " properties).");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onSetLocation() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }

        std::string cc = countryCodeEdit->text().trimmed().toStdString();
        std::string city = cityEdit->text().trimmed().toStdString();
        std::string gps = gpsEdit->text().trimmed().toStdString();
        std::string ip = ipEdit->text().trimmed().toStdString();

        try {
            Countly::getInstance().setLocation(cc, city, gps, ip);
            logMsg("[App] Location set - Country: " + cc
                   + ", City: " + city
                   + ", GPS: " + gps
                   + ", IP: " + ip);
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    void onFetchFeedbackWidgets() {
        std::string serverUrl = serverUrlEdit->text().trimmed().toStdString();
        std::string appKey = appKeyEdit->text().trimmed().toStdString();
        std::string deviceId = deviceIdEdit->text().trimmed().toStdString();

        if (serverUrl.empty() || appKey.empty() || deviceId.empty()) {
            logMsg("[Widgets] Server URL, App Key, and Device ID are required (see Init tab).");
            return;
        }

        // Clear existing cards (keep header at index 0 and stretch at the end).
        while (fwCardsLayout->count() > 2) {
            QLayoutItem *item = fwCardsLayout->takeAt(1);
            if (item) {
                if (item->widget()) item->widget()->deleteLater();
                delete item;
            }
        }

        logMsg("[Widgets] Fetching widgets from " + serverUrl + " ...");
        std::vector<CountlyFeedbackWidget> widgets;
        try {
            widgets = fwFetchWidgets(serverUrl, appKey, deviceId);
        } catch (const std::exception &e) {
            logMsg(std::string("[Widgets][Error] ") + e.what());
            return;
        }

        fwHeaderLabel->setText(QString("Widgets (%1)").arg(widgets.size()));
        logMsg("[Widgets] Found " + std::to_string(widgets.size()) + " widgets");

        int insertAt = 1; // after header
        for (const auto &w : widgets) {
            auto *card = new WidgetCard(w);
            connect(card, &WidgetCard::clicked, this, &MainWindow::onFeedbackWidgetSelected);
            fwCardsLayout->insertWidget(insertAt++, card);
        }
    }

    void onFeedbackWidgetSelected(const CountlyFeedbackWidget &widget) {
        std::string serverUrl = serverUrlEdit->text().trimmed().toStdString();
        std::string appKey = appKeyEdit->text().trimmed().toStdString();
        std::string deviceId = deviceIdEdit->text().trimmed().toStdString();

        std::string url = fwConstructWebViewUrl(widget, serverUrl, appKey, deviceId);
        logMsg("[Widgets] Present " + widget.type + " - \"" + widget.name + "\"");
        logMsg("[Widgets] URL: " + url);

        fwWebPage->currentWidget = widget;
        fwWebView->setVisible(false);
        fwRightStack->setCurrentIndex(1);
        fwLoadTimer->start();
        fwWebView->setUrl(QUrl(QString::fromStdString(url)));
    }

    void onChangeDeviceId() {
        if (!sdkInitialized) { logMsg("[App] SDK not initialized."); return; }

        std::string newId = newDeviceIdEdit->text().trimmed().toStdString();
        bool merge = mergeCheck->isChecked();

        if (newId.empty()) {
            logMsg("[App] New device ID is required.");
            return;
        }

        try {
            Countly::getInstance().setDeviceID(newId, merge);
            logMsg("[App] Device ID changed to: \"" + newId
                   + "\" (merge=" + (merge ? "true" : "false") + ")");
        } catch (const std::exception &e) {
            logMsg(std::string("[App][Error] ") + e.what());
        }
    }

    // Static instance pointer for the logger callback
    static MainWindow *sInstance;
    friend int main(int argc, char *argv[]);
};

MainWindow *MainWindow::sInstance = nullptr;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet(GLOBAL_STYLE);

    MainWindow window;
    MainWindow::sInstance = &window;

    // Connect the thread-safe log signal
    QObject::connect(&window, &MainWindow::logMessageReceived,
                     &window, &MainWindow::appendLog,
                     Qt::QueuedConnection);

    window.show();
    int result = app.exec();

    // Stop SDK and clear the instance pointer BEFORE window is destroyed,
    // so background threads don't call logMsg on a dead MainWindow.
    MainWindow::sInstance = nullptr;
    Countly::getInstance().stop();

    return result;
}

#include "main.moc"
