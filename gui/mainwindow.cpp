#include "mainwindow.h"

#include "engine/executionhandler.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include "order_entry_widget.h"
#include "orderbook_widget.h"
#include "performance_dashboard.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace {
constexpr int kInstrumentCount = 5;
constexpr int kPageOrderBook = 0;
constexpr int kPageExecutionReports = 1;
constexpr int kPageManualEntry = 2;
constexpr int kPagePerformance = 3;
constexpr int kSidebarWidth = 220;
constexpr int kTopBarHeight = 64;
constexpr int kBottomBarHeight = 60;
constexpr int kPageMargin = 24;
constexpr int kTableRowHeight = 44;

QColor rowBackgroundForStatus(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#f7f9fd");
    case Status::Fill:
        return QColor("#f5faf7");
    case Status::PFill:
        return QColor("#fdf8f4");
    case Status::Rejected:
        return QColor("#fdf5f5");
    }
    return QColor("#ffffff");
}

QColor rowStatusColor(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#5b8fcf");
    case Status::Fill:
        return QColor("#2d6b4a");
    case Status::PFill:
        return QColor("#c2855a");
    case Status::Rejected:
        return QColor("#b84a4a");
    }
    return QColor("#52443c");
}

QColor sideBackground(Side side) {
    return side == Side::Buy ? QColor("#e8f4ee") : QColor("#fdf0f0");
}

QColor sideForeground(Side side) {
    return side == Side::Buy ? QColor("#2d6b4a") : QColor("#9e3a3a");
}

void buildSyntheticBookFromReports(const std::vector<ExecutionReport> &reports,
                                   Instrument instrument, std::vector<BookEntry> &buys,
                                   std::vector<BookEntry> &sells) {
    buys.clear();
    sells.clear();

    uint32_t seqNum = 0;
    for (const ExecutionReport &report : reports) {
        if (report.instrument != instrument || report.status != Status::New) {
            continue;
        }

        BookEntry entry{};
        entry.order.price = report.price;
        entry.order.quantity = report.quantity;
        entry.order.instrument = report.instrument;
        entry.order.side = report.side;
        entry.remainingQty = report.quantity;
        entry.seqNum = seqNum++;
        std::memcpy(entry.order.clientOrderId, report.clientOrderId,
                    sizeof(entry.order.clientOrderId));
        std::memcpy(entry.orderId, report.orderId, sizeof(entry.orderId));

        if (report.side == Side::Buy) {
            buys.push_back(entry);
        } else if (report.side == Side::Sell) {
            sells.push_back(entry);
        }
    }

    std::sort(buys.begin(), buys.end(), [](const BookEntry &lhs, const BookEntry &rhs) {
        if (lhs.order.price != rhs.order.price) {
            return lhs.order.price > rhs.order.price;
        }
        return lhs.seqNum < rhs.seqNum;
    });

    std::sort(sells.begin(), sells.end(), [](const BookEntry &lhs, const BookEntry &rhs) {
        if (lhs.order.price != rhs.order.price) {
            return lhs.order.price < rhs.order.price;
        }
        return lhs.seqNum < rhs.seqNum;
    });
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    qRegisterMetaType<Instrument>("Instrument");

    setWindowTitle("LillyLedger");
    resize(1280, 820);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(central);

    setupSidebar();
    rootLayout->addWidget(sidebarWidget_);

    auto *mainContent = new QWidget(central);
    auto *mainLayout = new QVBoxLayout(mainContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupTopBar();
    mainLayout->addWidget(topBarWidget_);

    setupCentralArea();
    mainLayout->addWidget(stackedWidget_, 1);

    setupBottomBar();
    mainLayout->addWidget(bottomBarWidget_);

    rootLayout->addWidget(mainContent, 1);

    connect(this, &MainWindow::currentInstrumentChanged, this,
            [this](Instrument) { updateOrderBookTab(); });

    applyLightTheme();
    setCurrentPage(kPageExecutionReports);
    refreshUiState();
    setLedState(LedState::Idle);
    updateOrderBookTab();
}

void MainWindow::setupSidebar() {
    sidebarWidget_ = new QWidget(this);
    sidebarWidget_->setFixedWidth(kSidebarWidth);
    sidebarWidget_->setObjectName("Sidebar");

    auto *layout = new QVBoxLayout(sidebarWidget_);
    layout->setContentsMargins(16, 28, 16, 28);
    layout->setSpacing(2);

    auto *logoContainer = new QWidget(sidebarWidget_);
    auto *logoLayout = new QVBoxLayout(logoContainer);
    logoLayout->setContentsMargins(0, 0, 0, 32);
    logoLayout->setSpacing(6);

    auto *logo = new QLabel("LillyLedger", logoContainer);
    logo->setObjectName("Logo");
    auto *subtitle = new QLabel("PREMIUM TRADING", logoContainer);
    subtitle->setObjectName("LogoSubtitle");

    logoLayout->addWidget(logo);
    logoLayout->addWidget(subtitle);
    layout->addWidget(logoContainer);

    auto createNavBtn = [this](const QString &text, bool active) {
        auto *btn = new QPushButton(text, sidebarWidget_);
        btn->setCheckable(true);
        btn->setChecked(active);
        btn->setProperty("navIcon", true);
        return btn;
    };

    sidebarInstrumentsButton_ = createNavBtn("  Instruments", false);
    sidebarAnalyticsButton_ = createNavBtn("  Analytics", true);
    sidebarManualButton_ = createNavBtn("  Manual Entry", false);
    sidebarPerformanceButton_ = createNavBtn("  Performance", false);

    auto *navHeader = new QLabel("NAVIGATION", sidebarWidget_);
    navHeader->setStyleSheet(
        "QLabel {"
        "  color: #4a3f38;"
        "  font-size: 9px;"
        "  font-weight: 700;"
        "  letter-spacing: 2px;"
        "  padding: 0px 18px;"
        "  margin-bottom: 4px;"
        "}");

    layout->addWidget(navHeader);
    layout->addWidget(sidebarInstrumentsButton_);
    layout->addWidget(sidebarAnalyticsButton_);
    layout->addWidget(sidebarManualButton_);
    layout->addWidget(sidebarPerformanceButton_);

    layout->addStretch();
    sidebarSupportButton_ = createNavBtn("Support", false);
    layout->addWidget(sidebarSupportButton_);

    connect(sidebarInstrumentsButton_, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageOrderBook); });
    connect(sidebarAnalyticsButton_, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageExecutionReports); });
    connect(sidebarManualButton_, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageManualEntry); });
    connect(sidebarPerformanceButton_, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPagePerformance); });
    connect(sidebarSupportButton_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "Support",
                                 "Need help with LillyLedger?\n"
                                 "1. Load an input CSV\n"
                                 "2. Run Engine\n"
                                 "3. Inspect reports, order book and performance.");
    });
}

void MainWindow::setupTopBar() {
    topBarWidget_ = new QWidget(this);
    topBarWidget_->setFixedHeight(kTopBarHeight);
    topBarWidget_->setObjectName("TopBar");

    auto *layout = new QHBoxLayout(topBarWidget_);
    layout->setContentsMargins(32, 0, 32, 0);

    auto *navContainer = new QWidget(topBarWidget_);
    auto *navLayout = new QHBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(8);

    auto createTabBtn = [this](const QString &text, bool active) {
        auto *btn = new QPushButton(text, topBarWidget_);
        btn->setCheckable(true);
        btn->setChecked(active);
        btn->setObjectName("TopTabBtn");
        topTabButtons_.push_back(btn);
        return btn;
    };

    auto *orderBookButton = createTabBtn("Order Book", false);
    auto *reportsButton = createTabBtn("Execution Reports", true);
    auto *manualButton = createTabBtn("Manual Entry", false);
    auto *performanceButton = createTabBtn("Performance", false);

    navLayout->addWidget(orderBookButton);
    navLayout->addWidget(reportsButton);
    navLayout->addWidget(manualButton);
    navLayout->addWidget(performanceButton);
    layout->addWidget(navContainer);

    connect(orderBookButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageOrderBook); });
    connect(reportsButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageExecutionReports); });
    connect(manualButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageManualEntry); });
    connect(performanceButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPagePerformance); });

    layout->addStretch();

    openCsvButton_ = new QPushButton("Open CSV", topBarWidget_);
    openCsvButton_->setObjectName("OpenCsvBtn");
    connect(openCsvButton_, &QPushButton::clicked, this, [this]() {
        const QString filePath =
            QFileDialog::getOpenFileName(this, "Open CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            loadInputFile(filePath);
        }
    });

    runEngineButton_ = new QPushButton("Run Engine", topBarWidget_);
    runEngineButton_->setObjectName("RunEngineBtn");
    runEngineButton_->setEnabled(false);
    connect(runEngineButton_, &QPushButton::clicked, this, &MainWindow::runEngine);

    layout->addWidget(openCsvButton_);
    layout->addWidget(runEngineButton_);
}

void MainWindow::setupCentralArea() {
    stackedWidget_ = new QStackedWidget(this);

    auto *orderBookTab = new QWidget(stackedWidget_);
    auto *orderBookLayout = new QVBoxLayout(orderBookTab);
    orderBookLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    orderBookLayout->setSpacing(12);

    auto *orderHeader = new QWidget(orderBookTab);
    orderHeader->setObjectName("PageHeader");
    auto *orderHeaderLayout = new QHBoxLayout(orderHeader);
    orderHeaderLayout->setContentsMargins(24, 12, 24, 12);

    auto *orderTitle = new QLabel("ORDER BOOK", orderHeader);
    orderTitle->setObjectName("PageTitle");
    orderHeaderLayout->addWidget(orderTitle);
    orderHeaderLayout->addStretch();

    orderBookInstrumentCombo_ = new QComboBox(orderHeader);
    orderBookInstrumentCombo_->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    orderBookInstrumentCombo_->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    orderBookInstrumentCombo_->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    orderBookInstrumentCombo_->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    orderBookInstrumentCombo_->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));
    orderHeaderLayout->addWidget(orderBookInstrumentCombo_);

    connect(orderBookInstrumentCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || index >= kInstrumentCount) {
                    return;
                }
                emit currentInstrumentChanged(static_cast<Instrument>(index));
            });

    orderBookLayout->addWidget(orderHeader);

    orderBookWidget_ = new OrderBookWidget(orderBookTab);
    orderBookLayout->addWidget(orderBookWidget_, 1);

    stackedWidget_->addWidget(orderBookTab);

    auto *reportsTab = new QWidget(stackedWidget_);
    auto *reportsLayout = new QVBoxLayout(reportsTab);
    reportsLayout->setContentsMargins(0, 0, 0, 0);
    reportsLayout->setSpacing(0);

    auto *headerWidget = new QWidget(reportsTab);
    headerWidget->setObjectName("PageHeader");
    headerWidget->setMinimumHeight(88);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(32, 24, 32, 24);

    auto *titleContainer = new QWidget(headerWidget);
    auto *titleLayout = new QVBoxLayout(titleContainer);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);
    auto *title = new QLabel("EXECUTION REPORTS", titleContainer);
    title->setObjectName("PageTitle");
    reportsSubtitleLabel_ = new QLabel("No reports yet", titleContainer);
    reportsSubtitleLabel_->setObjectName("PageSubtitle");
    titleLayout->addWidget(title);
    titleLayout->addWidget(reportsSubtitleLabel_);
    headerLayout->addWidget(titleContainer);

    exportCsvButton_ = new QPushButton("Export CSV", headerWidget);
    exportCsvButton_->setObjectName("ExportCsvBtn");
    exportCsvButton_->setEnabled(false);
    exportCsvButton_->setFixedHeight(36);
    exportCsvButton_->setMinimumWidth(120);
    exportCsvButton_->setMaximumWidth(160);
    connect(exportCsvButton_, &QPushButton::clicked, this, [this]() {
        const QString filePath =
            QFileDialog::getSaveFileName(this, "Save CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            exportResults(filePath);
        }
    });
    headerLayout->addStretch();
    headerLayout->addWidget(exportCsvButton_, 0, Qt::AlignVCenter);

    auto *filterBar = new QWidget(reportsTab);
    filterBar->setObjectName("FilterBar");
    filterBar->setMinimumHeight(52);
    auto *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(32, 0, 32, 0);
    filterLayout->setSpacing(12);

    instrumentFilter_ = new QComboBox(filterBar);
    instrumentFilter_->addItem("All Instruments", -1);
    instrumentFilter_->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    instrumentFilter_->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    instrumentFilter_->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    instrumentFilter_->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    instrumentFilter_->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));

    statusFilter_ = new QComboBox(filterBar);
    statusFilter_->addItem("All Status", -1);
    statusFilter_->addItem("New", static_cast<int>(Status::New));
    statusFilter_->addItem("Rejected", static_cast<int>(Status::Rejected));
    statusFilter_->addItem("Fill", static_cast<int>(Status::Fill));
    statusFilter_->addItem("PFill", static_cast<int>(Status::PFill));

    sideFilter_ = new QComboBox(filterBar);
    sideFilter_->addItem("All Sides", -1);
    sideFilter_->addItem("Buy", static_cast<int>(Side::Buy));
    sideFilter_->addItem("Sell", static_cast<int>(Side::Sell));

    filterLayout->addWidget(instrumentFilter_);
    filterLayout->addWidget(statusFilter_);
    filterLayout->addWidget(sideFilter_);
    filterLayout->addStretch();

    showingResultsLabel_ = new QLabel("Showing 0 / 0 results", filterBar);
    showingResultsLabel_->setObjectName("ResultsLabel");
    filterLayout->addWidget(showingResultsLabel_);

    connect(instrumentFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });
    connect(statusFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });
    connect(sideFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });

    auto *tableContainer = new QWidget(reportsTab);
    auto *tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    auto *tableWrapper = new QWidget(tableContainer);
    tableWrapper->setObjectName("TableWrapper");
    auto *wrapperLayout = new QVBoxLayout(tableWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    executionReportsTable_ = new QTableWidget(tableWrapper);
    executionReportsTable_->setColumnCount(7);
    executionReportsTable_->setHorizontalHeaderLabels({"Order ID", "Client Order ID", "Instrument",
                                                       "Side", "Exec Status", "Quantity",
                                                       "Price"});
    executionReportsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    executionReportsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    executionReportsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    executionReportsTable_->setShowGrid(false);
    executionReportsTable_->setAlternatingRowColors(false);
    executionReportsTable_->setWordWrap(false);
    executionReportsTable_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    executionReportsTable_->verticalHeader()->setVisible(false);
    executionReportsTable_->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    executionReportsTable_->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    executionReportsTable_->horizontalHeader()->setStretchLastSection(true);
    executionReportsTable_->horizontalHeader()->setHighlightSections(false);
    auto *reportsHeader = executionReportsTable_->horizontalHeader();
    reportsHeader->setMinimumSectionSize(56);
    reportsHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(6, QHeaderView::Stretch);

    wrapperLayout->addWidget(executionReportsTable_);

    auto *tableSummary = new QWidget(tableWrapper);
    tableSummary->setObjectName("TableSummary");
    tableSummary->setMinimumHeight(52);
    auto *summaryLayout = new QHBoxLayout(tableSummary);
    summaryLayout->setContentsMargins(32, 0, 32, 0);

    auto *totLabel = new QLabel("TOTAL", tableSummary);
    totLabel->setObjectName("SummaryLabel");
    summaryTotalValue_ = new QLabel("0", tableSummary);
    summaryTotalValue_->setObjectName("SummaryValue");
    summaryLayout->addWidget(totLabel);
    summaryLayout->addWidget(summaryTotalValue_);

    auto *vline = new QWidget(tableSummary);
    vline->setFixedSize(1, 16);
    vline->setStyleSheet("background-color: #e8e2d9; margin-left: 24px; margin-right: 24px;");
    summaryLayout->addWidget(vline);

    auto addSummaryItem = [tableSummary, summaryLayout](const QString &title, const QString &color,
                                                        QLabel **valueLabel) {
        auto *container = new QWidget(tableSummary);
        auto *containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(8);

        auto *titleLabel = new QLabel(title, container);
        titleLabel->setObjectName("SummaryLabel");
        *valueLabel = new QLabel("0 ●", container);
        (*valueLabel)
            ->setStyleSheet(
                QString(
                    "font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: %1;")
                    .arg(color));

        containerLayout->addWidget(titleLabel);
        containerLayout->addWidget(*valueLabel);
        summaryLayout->addWidget(container);
    };

    addSummaryItem("FILLED", "#2c694d", &summaryFilledValue_);
    addSummaryItem("PARTIAL", "#c2855a", &summaryPartialValue_);
    addSummaryItem("REJECTED", "#a33b3c", &summaryRejectedValue_);
    addSummaryItem("NEW", "#5b8fcf", &summaryNewValue_);
    summaryLayout->addStretch();

    wrapperLayout->addWidget(tableSummary);
    tableLayout->addWidget(tableWrapper);

    reportsLayout->addWidget(headerWidget);
    reportsLayout->addWidget(filterBar);
    reportsLayout->addWidget(tableContainer, 1);
    stackedWidget_->addWidget(reportsTab);

    manualEntryWidget_ = new ManualOrderEntryWidget(stackedWidget_);
    stackedWidget_->addWidget(manualEntryWidget_);

    performanceDashboard_ = new PerformanceDashboard(stackedWidget_);
    stackedWidget_->addWidget(performanceDashboard_);

    connect(manualEntryWidget_, &ManualOrderEntryWidget::orderSubmitted, this,
            [this](const Order &order) {
                const auto reports = router_.route(order);
                ordersProcessed_ += 1;
                executionReports_.insert(executionReports_.end(), reports.begin(), reports.end());

                updateExecutionReportsTable();
                updateOrderBookTab();
                refreshUiState();
                setLedState(LedState::Idle);
            });

    connect(orderBookWidget_, &OrderBookWidget::priceClicked, manualEntryWidget_,
            &ManualOrderEntryWidget::prefillPrice);
}

void MainWindow::setupBottomBar() {
    bottomBarWidget_ = new QWidget(this);
    bottomBarWidget_->setFixedHeight(kBottomBarHeight);
    bottomBarWidget_->setObjectName("BottomBar");

    auto *layout = new QHBoxLayout(bottomBarWidget_);
    layout->setContentsMargins(28, 0, 28, 0);
    layout->setSpacing(24);

    auto createBottomValueLabel = [this](const QString &objectName) {
        auto *label = new QLabel(this);
        label->setObjectName(objectName);
        return label;
    };

    ordersProcessedValue_ = createBottomValueLabel("BottomMetricMuted");
    reportsGeneratedValue_ = createBottomValueLabel("BottomMetricPositive");
    lastRunDurationValue_ = createBottomValueLabel("BottomMetricMuted");
    engineStateValue_ = createBottomValueLabel("BottomMetricState");

    // Left group — engine stats
    layout->addWidget(ordersProcessedValue_);

    auto *divider1 = new QWidget(bottomBarWidget_);
    divider1->setFixedSize(1, 16);
    divider1->setStyleSheet("background-color: #ddd0c6;");
    layout->addWidget(divider1);

    layout->addWidget(reportsGeneratedValue_);

    auto *divider2 = new QWidget(bottomBarWidget_);
    divider2->setFixedSize(1, 16);
    divider2->setStyleSheet("background-color: #ddd0c6;");
    layout->addWidget(divider2);

    layout->addWidget(lastRunDurationValue_);

    layout->addStretch();

    // Right — engine status
    layout->addWidget(engineStateValue_);
}

void MainWindow::applyLightTheme() {
    // ── Font families with proper system fallbacks ────────────────────────────
    // 'Georgia' and 'Palatino Linotype' are available on all platforms and give
    // the same editorial serif character as Newsreader without requiring install.
    // 'Courier New' is the universal monospace fallback when IBM Plex Mono is absent.

    qApp->setStyleSheet(R"(

/* ── Reset & base ──────────────────────────────────────────────────── */
QMainWindow, QDialog { background-color: #faf8f4; }

/* Single authoritative font size — do NOT set font-size on QWidget globally.
   Instead set it per-widget type to avoid the cascade fight. */
QLabel      { font-size: 13px; color: #3b312b; font-family: 'DM Sans', 'Segoe UI', sans-serif; }
QPushButton { font-size: 13px; font-family: 'DM Sans', 'Segoe UI', sans-serif; min-height: 36px; }
QComboBox   { font-size: 13px; font-family: 'DM Sans', 'Segoe UI', sans-serif; min-height: 36px; }
QLineEdit   { font-size: 13px; font-family: 'DM Sans', 'Segoe UI', sans-serif; min-height: 36px; }
QSpinBox, QDoubleSpinBox {
    font-size: 13px;
    font-family: 'Courier New', 'Consolas', monospace;
    min-height: 36px;
}

/* ── Sidebar ───────────────────────────────────────────────────────── */
#Sidebar { background-color: #1c1917; }

#Logo {
    color: #c2855a;
    font-family: 'Georgia', 'Palatino Linotype', serif;
    font-size: 22px;
    font-style: italic;
    font-weight: 400;
}
#LogoSubtitle {
    color: #7a6a5e;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 2px;
}

QPushButton[navIcon="true"] {
    text-align: left;
    padding: 11px 18px;
    border: none;
    border-radius: 0px;
    color: #8a7a6e;
    font-weight: 500;
    font-size: 13px;
    background-color: transparent;
    min-height: 44px;
}
QPushButton[navIcon="true"]:hover {
    background-color: #2a2522;
    color: #d4c9b8;
}
QPushButton[navIcon="true"]:checked {
    color: #ffffff;
    font-weight: 600;
    background-color: #2a2522;
    border-left: 3px solid #c2855a;
    padding-left: 15px;
}

/* ── Top bar ───────────────────────────────────────────────────────── */
#TopBar {
    background-color: #ffffff;
    border-bottom: 1px solid #e8e2d9;
}

#TopTabBtn {
    border: none;
    border-bottom: 3px solid transparent;
    background: transparent;
    color: #9a8a7e;
    font-weight: 500;
    font-size: 13px;
    padding: 0px 4px;
    margin: 0px 2px;
    min-height: 64px;
    min-width: 80px;
}
#TopTabBtn:hover  { color: #86522b; }
#TopTabBtn:checked {
    color: #6b3d18;
    border-bottom: 3px solid #c2855a;
    font-weight: 700;
}

#OpenCsvBtn {
    border: 1.5px solid #c2855a;
    border-radius: 8px;
    color: #86522b;
    background-color: transparent;
    font-weight: 700;
    font-size: 12px;
    padding: 0px 18px;
    min-height: 36px;
    min-width: 90px;
}
#OpenCsvBtn:hover { background-color: #fdf4ee; }

#RunEngineBtn {
    background-color: #86522b;
    border: none;
    border-radius: 8px;
    color: #ffffff;
    font-weight: 700;
    font-size: 12px;
    padding: 0px 18px;
    min-height: 36px;
    min-width: 100px;
}
#RunEngineBtn:hover    { background-color: #74461f; }
#RunEngineBtn:pressed  { background-color: #613a19; }
#RunEngineBtn:disabled { background-color: #d6c3b7; color: #f0e8e2; }

/* ── Page headers ──────────────────────────────────────────────────── */
#PageHeader {
    background-color: #ffffff;
    border-bottom: 1px solid #e8e2d9;
}
#PageTitle {
    color: #1e1b19;
    font-family: 'Georgia', 'Palatino Linotype', serif;
    font-size: 18px;
    font-weight: 400;
    letter-spacing: 1px;
}
#PageSubtitle {
    color: #9a8a7e;
    font-size: 12px;
    font-weight: 400;
}
#ExportCsvBtn {
    color: #86522b;
    font-weight: 700;
    font-size: 12px;
    border: 1.5px solid #c2855a;
    border-radius: 8px;
    background: transparent;
    padding: 0px 14px;
    min-height: 34px;
}
#ExportCsvBtn:hover    { background-color: #fdf4ee; }
#ExportCsvBtn:disabled { color: #c4b4aa; border-color: #e0d0c8; }

/* ── Filter bar ────────────────────────────────────────────────────── */
#FilterBar {
    background-color: #f5ede7;
    border-bottom: 1px solid #ead8ce;
}

QComboBox {
    background-color: #ffffff;
    border: 1.5px solid #ddd0c6;
    border-radius: 8px;
    padding: 0px 10px;
    color: #3b312b;
    font-weight: 500;
    font-size: 12px;
    min-height: 34px;
}
QComboBox:hover { border-color: #c2855a; }
QComboBox:focus { border-color: #c2855a; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border: none;
}
QComboBox::down-arrow {
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #84746a;
    margin-right: 6px;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #3b312b;
    border: 1px solid #ddd0c6;
    border-radius: 4px;
    selection-background-color: #f5ede7;
    selection-color: #1e1b19;
    outline: 0px;
    padding: 4px;
}

#ResultsLabel {
    color: #9a8a7e;
    font-weight: 500;
    font-size: 11px;
}

/* ── Tables ────────────────────────────────────────────────────────── */
#TableWrapper {
    background-color: #ffffff;
    border: 1px solid #ead8ce;
    border-radius: 10px;
}
QTableWidget {
    border: none;
    background-color: transparent;
    gridline-color: #f0e8e2;
    font-size: 13px;
    color: #3b312b;
    font-family: 'DM Sans', 'Segoe UI', sans-serif;
}
QTableWidget::item {
    padding: 4px 12px;
    border-bottom: 1px solid #f0e8e2;
}
QTableWidget::item:selected {
    background-color: #fdf4ee;
    color: #1e1b19;
}
QHeaderView::section {
    background-color: #f5ede7;
    color: #7a6a5e;
    border: none;
    border-bottom: 1.5px solid #ead8ce;
    border-right: 1px solid #ead8ce;
    padding: 8px 12px;
    font-weight: 700;
    font-size: 11px;
    letter-spacing: 0.5px;
    font-family: 'DM Sans', 'Segoe UI', sans-serif;
}
QHeaderView::section:last { border-right: none; }

/* ── Summary bar ───────────────────────────────────────────────────── */
#TableSummary {
    background-color: #faf8f4;
    border-top: 1px solid #ead8ce;
}
#SummaryLabel {
    color: #9a8a7e;
    font-weight: 700;
    font-size: 10px;
    letter-spacing: 1px;
}
#SummaryValue {
    color: #1e1b19;
    font-weight: 700;
    font-family: 'Courier New', 'Consolas', monospace;
    font-size: 14px;
}

/* ── Scrollbars — keep them subtle and on-theme ─────────────────────── */
QScrollBar:vertical {
    background: #faf8f4;
    width: 8px;
    margin: 0;
    border-radius: 4px;
}
QScrollBar::handle:vertical {
    background: #d6c3b7;
    border-radius: 4px;
    min-height: 32px;
}
QScrollBar::handle:vertical:hover { background: #c2855a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

QScrollBar:horizontal {
    background: #faf8f4;
    height: 8px;
    margin: 0;
    border-radius: 4px;
}
QScrollBar::handle:horizontal {
    background: #d6c3b7;
    border-radius: 4px;
    min-width: 32px;
}
QScrollBar::handle:horizontal:hover { background: #c2855a; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }

/* ── Input fields ──────────────────────────────────────────────────── */
QLineEdit {
    background-color: #ffffff;
    border: 1.5px solid #ddd0c6;
    border-radius: 8px;
    padding: 0px 12px;
    color: #1e1b19;
    selection-background-color: #f5ede7;
}
QLineEdit:hover { border-color: #c2855a; }
QLineEdit:focus { border-color: #86522b; }

QSpinBox, QDoubleSpinBox {
    background-color: #ffffff;
    border: 1.5px solid #ddd0c6;
    border-radius: 8px;
    padding: 0px 8px;
    color: #1e1b19;
}
QSpinBox:hover, QDoubleSpinBox:hover   { border-color: #c2855a; }
QSpinBox:focus, QDoubleSpinBox:focus   { border-color: #86522b; }
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    width: 20px;
    border: none;
    background: transparent;
}

/* ── QGroupBox (used in Manual Entry) ─────────────────────────────── */
QGroupBox {
    background-color: #ffffff;
    border: 1px solid #ead8ce;
    border-radius: 10px;
    margin-top: 14px;
    padding: 16px 20px 20px 20px;
    font-size: 11px;
    font-weight: 700;
    color: #9a8a7e;
    letter-spacing: 1px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 16px;
    top: 0px;
    padding: 0px 6px;
    background-color: #faf8f4;
    color: #9a8a7e;
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 1.5px;
}

/* ── Bottom status bar ─────────────────────────────────────────────── */
#BottomBar {
    background-color: #ffffff;
    border-top: 1px solid #e8e2d9;
}
#BottomMetricMuted, #BottomMetricPositive, #BottomMetricState {
    font-weight: 600;
    font-size: 11px;
    letter-spacing: 0.5px;
    font-family: 'DM Sans', 'Segoe UI', sans-serif;
}
#BottomMetricMuted    { color: #9a8a7e; }
#BottomMetricPositive { color: #2c694d; }
#BottomMetricState[engineState="idle"]    { color: #2c694d; }
#BottomMetricState[engineState="running"] { color: #c2855a; }
#BottomMetricState[engineState="error"]   { color: #a33b3c; }

    )");
}

void MainWindow::setLedState(LedState state) {
    if (engineStateValue_ == nullptr) {
        return;
    }

    if (state == LedState::Running) {
        engineStateValue_->setProperty("engineState", "running");
        engineStateValue_->setText("STATUS RUNNING");
        engineStateValue_->style()->unpolish(engineStateValue_);
        engineStateValue_->style()->polish(engineStateValue_);
        return;
    }

    if (state == LedState::Error) {
        engineStateValue_->setProperty("engineState", "error");
        engineStateValue_->setText("STATUS ERROR");
        engineStateValue_->style()->unpolish(engineStateValue_);
        engineStateValue_->style()->polish(engineStateValue_);
        return;
    }

    engineStateValue_->setProperty("engineState", "idle");
    engineStateValue_->setText("STATUS IDLE");
    engineStateValue_->style()->unpolish(engineStateValue_);
    engineStateValue_->style()->polish(engineStateValue_);
}

void MainWindow::refreshUiState() {
    if (runEngineButton_ != nullptr) {
        runEngineButton_->setEnabled(!inputFilePath_.isEmpty());
    }
    if (exportCsvButton_ != nullptr) {
        exportCsvButton_->setEnabled(!executionReports_.empty());
    }

    const int totalReports = static_cast<int>(executionReports_.size());
    if (reportsSubtitleLabel_ != nullptr) {
        if (totalReports == 0) {
            reportsSubtitleLabel_->setText("No reports yet");
        } else {
            reportsSubtitleLabel_->setText(
                QString("%1 records from last engine run").arg(totalReports));
        }
    }

    if (ordersProcessedValue_ != nullptr) {
        ordersProcessedValue_->setText(
            QString("Orders Processed: %1").arg(QLocale().toString(ordersProcessed_)));
    }
    if (reportsGeneratedValue_ != nullptr) {
        reportsGeneratedValue_->setText(
            QString("Reports Generated: %1").arg(QLocale().toString(totalReports)));
    }
    if (lastRunDurationValue_ != nullptr) {
        lastRunDurationValue_->setText(QString("Cycle: %1 ms").arg(lastRunDurationMs_));
    }

    const int shownRows =
        executionReportsTable_ != nullptr ? executionReportsTable_->rowCount() : totalReports;
    updateSummaryBar(shownRows);
}

void MainWindow::updateSummaryBar(int filteredCount) {
    const int totalReports = static_cast<int>(executionReports_.size());
    int fillCount = 0;
    int pfillCount = 0;
    int rejectedCount = 0;
    int newCount = 0;

    for (const ExecutionReport &report : executionReports_) {
        switch (report.status) {
        case Status::Fill:
            ++fillCount;
            break;
        case Status::PFill:
            ++pfillCount;
            break;
        case Status::Rejected:
            ++rejectedCount;
            break;
        case Status::New:
            ++newCount;
            break;
        }
    }

    if (summaryTotalValue_ != nullptr) {
        summaryTotalValue_->setText(QLocale().toString(totalReports));
    }
    if (summaryFilledValue_ != nullptr) {
        summaryFilledValue_->setText(QString("%1 ●").arg(QLocale().toString(fillCount)));
    }
    if (summaryPartialValue_ != nullptr) {
        summaryPartialValue_->setText(QString("%1 ●").arg(QLocale().toString(pfillCount)));
    }
    if (summaryRejectedValue_ != nullptr) {
        summaryRejectedValue_->setText(QString("%1 ●").arg(QLocale().toString(rejectedCount)));
    }
    if (summaryNewValue_ != nullptr) {
        summaryNewValue_->setText(QString("%1 ●").arg(QLocale().toString(newCount)));
    }
    if (showingResultsLabel_ != nullptr) {
        showingResultsLabel_->setText(
            QString("Showing %1 / %2 results").arg(filteredCount).arg(totalReports));
    }
}

void MainWindow::updateExecutionReportsTable() {
    if (executionReportsTable_ == nullptr) {
        return;
    }

    const int instrumentFilterValue =
        instrumentFilter_ != nullptr ? instrumentFilter_->currentData().toInt() : -1;
    const int statusFilterValue =
        statusFilter_ != nullptr ? statusFilter_->currentData().toInt() : -1;
    const int sideFilterValue = sideFilter_ != nullptr ? sideFilter_->currentData().toInt() : -1;

    std::vector<const ExecutionReport *> filteredReports;
    filteredReports.reserve(executionReports_.size());

    for (const ExecutionReport &report : executionReports_) {
        if (instrumentFilterValue >= 0 &&
            static_cast<int>(report.instrument) != instrumentFilterValue) {
            continue;
        }
        if (statusFilterValue >= 0 && static_cast<int>(report.status) != statusFilterValue) {
            continue;
        }
        if (sideFilterValue >= 0 && static_cast<int>(report.side) != sideFilterValue) {
            continue;
        }
        filteredReports.push_back(&report);
    }

    executionReportsTable_->setRowCount(static_cast<int>(filteredReports.size()));

    const QLocale locale;
    for (int row = 0; row < static_cast<int>(filteredReports.size()); ++row) {
        const ExecutionReport &report = *filteredReports[static_cast<std::size_t>(row)];

        auto *orderItem = new QTableWidgetItem(fixedCharToQString(report.orderId, kOrderIdLen));
        auto *clientItem =
            new QTableWidgetItem(fixedCharToQString(report.clientOrderId, kClientOrderIdLen));
        auto *instrumentItem = new QTableWidgetItem(instrumentName(report.instrument));
        auto *sideItem = new QTableWidgetItem(sideName(report.side));
        auto *statusItem = new QTableWidgetItem(statusName(report.status));
        auto *qtyItem = new QTableWidgetItem(locale.toString(report.quantity));
        auto *priceItem = new QTableWidgetItem(locale.toString(report.price, 'f', 2));

        sideItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setTextAlignment(Qt::AlignCenter);
        qtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const QColor rowBg = rowBackgroundForStatus(report.status);
        const QColor statusColor = rowStatusColor(report.status);

        std::array<QTableWidgetItem *, 7> rowItems = {orderItem, clientItem, instrumentItem,
                                                      sideItem,  statusItem, qtyItem, priceItem};

        for (QTableWidgetItem *item : rowItems) {
            item->setBackground(rowBg);
            item->setForeground(QColor("#3b312b"));
        }

        sideItem->setBackground(sideBackground(report.side));
        sideItem->setForeground(sideForeground(report.side));

        statusItem->setForeground(statusColor);
        statusItem->setFont(
            QFont(statusItem->font().family(), statusItem->font().pointSize(), QFont::DemiBold));

        executionReportsTable_->setItem(row, 0, orderItem);
        executionReportsTable_->setItem(row, 1, clientItem);
        executionReportsTable_->setItem(row, 2, instrumentItem);
        executionReportsTable_->setItem(row, 3, sideItem);
        executionReportsTable_->setItem(row, 4, statusItem);
        executionReportsTable_->setItem(row, 5, qtyItem);
        executionReportsTable_->setItem(row, 6, priceItem);
    }

    updateSummaryBar(static_cast<int>(filteredReports.size()));
}

void MainWindow::updateOrderBookTab() {
    if (orderBookWidget_ == nullptr) {
        return;
    }

    const Instrument instrument = selectedInstrument();
    std::vector<BookEntry> buys;
    std::vector<BookEntry> sells;
    buildSyntheticBookFromReports(executionReports_, instrument, buys, sells);
    orderBookWidget_->updateBook(buys, sells, instrument);
}

void MainWindow::updatePerformanceTab(qint64 parseUs, qint64 matchUs, qint64 writeUs) {
    if (performanceDashboard_ == nullptr) {
        return;
    }

    PerfStats stats;
    stats.parseUs = parseUs;
    stats.matchUs = matchUs;
    stats.writeUs = writeUs;
    stats.totalUs = parseUs + matchUs + writeUs;
    stats.orderCount = static_cast<int>(ordersProcessed_);
    stats.reportCount = static_cast<int>(executionReports_.size());

    performanceDashboard_->addRunHistory(stats, executionReports_);
}

void MainWindow::setCurrentPage(int index) {
    if (stackedWidget_ == nullptr) {
        return;
    }

    if (index < 0 || index >= stackedWidget_->count()) {
        return;
    }

    stackedWidget_->setCurrentIndex(index);
    for (std::size_t i = 0; i < topTabButtons_.size(); ++i) {
        if (topTabButtons_[i] != nullptr) {
            topTabButtons_[i]->setChecked(static_cast<int>(i) == index);
        }
    }

    if (sidebarInstrumentsButton_ != nullptr) {
        sidebarInstrumentsButton_->setChecked(index == kPageOrderBook);
    }
    if (sidebarAnalyticsButton_ != nullptr) {
        sidebarAnalyticsButton_->setChecked(index == kPageExecutionReports);
    }
    if (sidebarManualButton_ != nullptr) {
        sidebarManualButton_->setChecked(index == kPageManualEntry);
    }
    if (sidebarPerformanceButton_ != nullptr) {
        sidebarPerformanceButton_->setChecked(index == kPagePerformance);
    }
    if (sidebarSupportButton_ != nullptr) {
        sidebarSupportButton_->setChecked(false);
    }

    if (index == kPageOrderBook) {
        updateOrderBookTab();
    }
}

void MainWindow::loadInputFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    inputFilePath_ = filePath;
    executionReports_.clear();
    ordersProcessed_ = 0;
    lastRunDurationMs_ = 0;
    router_ = OrderRouter{};

    updateExecutionReportsTable();
    updateOrderBookTab();
    refreshUiState();
    setLedState(LedState::Idle);

    const QFileInfo info(filePath);
    if (reportsSubtitleLabel_ != nullptr) {
        reportsSubtitleLabel_->setText(QString("Input loaded: %1").arg(info.fileName()));
    }
}

void MainWindow::runEngine() {
    if (inputFilePath_.isEmpty()) {
        QMessageBox::warning(this, "Run Engine", "Load an input CSV file first.");
        setLedState(LedState::Error);
        return;
    }

    setLedState(LedState::Running);

    const auto totalStart = std::chrono::steady_clock::now();

    const auto parseStart = std::chrono::steady_clock::now();
    CSVReader reader(inputFilePath_.toStdString());
    const std::vector<ParseResult> parseResults = reader.readAll();
    const auto parseEnd = std::chrono::steady_clock::now();

    router_ = OrderRouter{};

    const auto matchStart = std::chrono::steady_clock::now();
    std::vector<ExecutionReport> reports;
    reports.reserve(parseResults.size() * 2);

    for (const ParseResult &result : parseResults) {
        if (!result.ok) {
            reports.push_back(
                ExecutionHandler::buildRejectedReport(result.order.clientOrderId, result.reason));
            continue;
        }

        auto routedReports = router_.route(result.order);
        reports.insert(reports.end(), routedReports.begin(), routedReports.end());
    }
    const auto matchEnd = std::chrono::steady_clock::now();

    const bool fileOpenError = parseResults.size() == 1 && !parseResults.front().ok &&
                               parseResults.front().reason == "Failed to open input file";

    executionReports_ = std::move(reports);
    ordersProcessed_ = parseResults.size();
    lastRunDurationMs_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(matchEnd - totalStart).count();

    const qint64 parseUs =
        std::chrono::duration_cast<std::chrono::microseconds>(parseEnd - parseStart).count();
    const qint64 matchUs =
        std::chrono::duration_cast<std::chrono::microseconds>(matchEnd - matchStart).count();

    setLedState(fileOpenError ? LedState::Error : LedState::Idle);
    updateExecutionReportsTable();
    updateOrderBookTab();
    updatePerformanceTab(parseUs, matchUs, 0);
    refreshUiState();

    if (fileOpenError) {
        QMessageBox::critical(
            this, "Run Engine",
            "Failed to open the selected input file. Check the path and try again.");
    }
}

void MainWindow::exportResults(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    if (executionReports_.empty()) {
        QMessageBox::information(this, "Export Results",
                                 "No execution reports available. Run the engine first.");
        return;
    }

    CSVWriter writer(filePath.toStdString());
    writer.write(executionReports_);
    setLedState(LedState::Idle);
}

Instrument MainWindow::selectedInstrument() const {
    if (orderBookInstrumentCombo_ != nullptr) {
        const int index = orderBookInstrumentCombo_->currentIndex();
        if (index >= 0 && index < kInstrumentCount) {
            return static_cast<Instrument>(index);
        }
    }

    return Instrument::Rose;
}

QString MainWindow::instrumentName(Instrument instrument) {
    switch (instrument) {
    case Instrument::Rose:
        return "🌹 Rose";
    case Instrument::Lavender:
        return "💜 Lavender";
    case Instrument::Lotus:
        return "🪷 Lotus";
    case Instrument::Tulip:
        return "🌷 Tulip";
    case Instrument::Orchid:
        return "🌸 Orchid";
    }

    return "Unknown";
}

QString MainWindow::sideName(Side side) {
    switch (side) {
    case Side::Buy:
        return "BUY";
    case Side::Sell:
        return "SELL";
    }

    return "Unknown";
}

QString MainWindow::statusName(Status status) {
    switch (status) {
    case Status::New:
        return "NEW";
    case Status::Rejected:
        return "REJECTED";
    case Status::Fill:
        return "FILL";
    case Status::PFill:
        return "PFILL";
    }

    return "Unknown";
}

QString MainWindow::compactTransactTime(const char *text) {
    const QString raw = fixedCharToQString(text, kTransactTimeLen);
    if (raw.size() < 18) {
        return raw;
    }

    const int dashIndex = raw.indexOf('-');
    if (dashIndex < 0 || dashIndex + 7 >= raw.size()) {
        return raw;
    }

    const QString hhmmssFraction = raw.mid(dashIndex + 1);
    if (hhmmssFraction.size() < 10) {
        return raw;
    }

    return QString("%1:%2:%3%4")
        .arg(hhmmssFraction.mid(0, 2))
        .arg(hhmmssFraction.mid(2, 2))
        .arg(hhmmssFraction.mid(4, 2))
        .arg(hhmmssFraction.mid(6));
}

QString MainWindow::fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}
