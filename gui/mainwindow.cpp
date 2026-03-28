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
        std::memcpy(entry.order.clientOrderId, report.clientOrderId, sizeof(entry.order.clientOrderId));
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
    sidebarWidget_->setFixedWidth(200);
    sidebarWidget_->setObjectName("Sidebar");

    auto *layout = new QVBoxLayout(sidebarWidget_);
    layout->setContentsMargins(24, 32, 24, 32);
    layout->setSpacing(4);

    auto *logoContainer = new QWidget(sidebarWidget_);
    auto *logoLayout = new QVBoxLayout(logoContainer);
    logoLayout->setContentsMargins(0, 0, 0, 48);
    logoLayout->setSpacing(4);

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

    sidebarInstrumentsButton_ = createNavBtn("eco  Instruments", false);
    sidebarAnalyticsButton_ = createNavBtn("monitoring  Analytics", true);
    sidebarManualButton_ = createNavBtn("potted_plant  Manual Entry", false);
    sidebarPerformanceButton_ = createNavBtn("settings  Performance", false);

    layout->addWidget(sidebarInstrumentsButton_);
    layout->addWidget(sidebarAnalyticsButton_);
    layout->addWidget(sidebarManualButton_);
    layout->addWidget(sidebarPerformanceButton_);

    layout->addStretch();
    sidebarSupportButton_ = createNavBtn("help  Support", false);
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
    topBarWidget_->setFixedHeight(64);
    topBarWidget_->setObjectName("TopBar");

    auto *layout = new QHBoxLayout(topBarWidget_);
    layout->setContentsMargins(32, 0, 32, 0);

    auto *navContainer = new QWidget(topBarWidget_);
    auto *navLayout = new QHBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(24);

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
    orderBookLayout->setContentsMargins(24, 24, 24, 24);
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
    headerWidget->setFixedHeight(88);
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

    exportCsvButton_ = new QPushButton("EXPORT CSV  download", headerWidget);
    exportCsvButton_->setObjectName("ExportCsvBtn");
    exportCsvButton_->setEnabled(false);
    connect(exportCsvButton_, &QPushButton::clicked, this, [this]() {
        const QString filePath =
            QFileDialog::getSaveFileName(this, "Save CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            exportResults(filePath);
        }
    });
    headerLayout->addWidget(exportCsvButton_, 0, Qt::AlignBottom);

    auto *filterBar = new QWidget(reportsTab);
    filterBar->setObjectName("FilterBar");
    filterBar->setFixedHeight(48);
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
    tableLayout->setContentsMargins(32, 32, 32, 32);

    auto *tableWrapper = new QWidget(tableContainer);
    tableWrapper->setObjectName("TableWrapper");
    auto *wrapperLayout = new QVBoxLayout(tableWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    executionReportsTable_ = new QTableWidget(tableWrapper);
    executionReportsTable_->setColumnCount(9);
    executionReportsTable_->setHorizontalHeaderLabels(
        {"Client ID", "Order ID", "Instrument", "Side", "Price", "Qty", "Status", "Reason",
         "Time"});
    executionReportsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    executionReportsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    executionReportsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    executionReportsTable_->setShowGrid(false);
    executionReportsTable_->setAlternatingRowColors(false);
    executionReportsTable_->verticalHeader()->setVisible(false);
    executionReportsTable_->horizontalHeader()->setStretchLastSection(true);
    executionReportsTable_->horizontalHeader()->setHighlightSections(false);

    executionReportsTable_->setColumnWidth(0, 120);
    executionReportsTable_->setColumnWidth(1, 110);
    executionReportsTable_->setColumnWidth(2, 140);
    executionReportsTable_->setColumnWidth(3, 80);
    executionReportsTable_->setColumnWidth(4, 90);
    executionReportsTable_->setColumnWidth(5, 90);
    executionReportsTable_->setColumnWidth(6, 90);
    executionReportsTable_->setColumnWidth(7, 240);
    executionReportsTable_->setColumnWidth(8, 110);

    wrapperLayout->addWidget(executionReportsTable_);

    auto *tableSummary = new QWidget(tableWrapper);
    tableSummary->setObjectName("TableSummary");
    tableSummary->setFixedHeight(52);
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
        (*valueLabel)->setStyleSheet(
            QString("font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: %1;")
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
    bottomBarWidget_->setFixedHeight(60);
    bottomBarWidget_->setObjectName("BottomBar");

    auto *layout = new QHBoxLayout(bottomBarWidget_);
    layout->setContentsMargins(28, 0, 28, 0);
    layout->setSpacing(24);

    auto createBottomValueLabel = [this](const QString &color) {
        auto *label = new QLabel(this);
        label->setStyleSheet(
            QString("font-weight: 700; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; "
                    "color: %1;")
                .arg(color));
        return label;
    };

    ordersProcessedValue_ = createBottomValueLabel("#84746a");
    reportsGeneratedValue_ = createBottomValueLabel("#2c694d");
    lastRunDurationValue_ = createBottomValueLabel("#84746a");
    engineStateValue_ = createBottomValueLabel("#2c694d");

    layout->addWidget(ordersProcessedValue_);
    layout->addStretch();
    layout->addWidget(reportsGeneratedValue_);
    layout->addStretch();
    layout->addWidget(lastRunDurationValue_);
    layout->addStretch();
    layout->addWidget(engineStateValue_);
}

void MainWindow::applyLightTheme() {
    qApp->setStyleSheet(R"(
        QMainWindow { background-color: #fff8f5; }

        #Sidebar { background-color: #1c1917; }
        #Logo { color: #c2855a; font-family: 'Newsreader'; font-size: 24px; font-style: italic; font-weight: 400; }
        #LogoSubtitle { color: #d4c9b8; opacity: 0.5; font-size: 10px; font-weight: 600; letter-spacing: 2px; text-transform: uppercase; }

        QPushButton[navIcon="true"] {
            text-align: left; padding: 12px 16px; border: none; border-radius: 0;
            color: rgba(212, 201, 184, 0.7); font-weight: 500; font-size: 14px; background-color: transparent;
        }
        QPushButton[navIcon="true"]:hover { background-color: #33302d; color: #ffffff; }
        QPushButton[navIcon="true"]:checked {
            color: #ffffff; font-weight: 700; border-left: 2px solid #c2855a; padding-left: 14px;
        }

        #TopBar { background-color: #ffffff; border-bottom: 1px solid #e8e2d9; }
        #TopTabBtn {
            border: none; border-bottom: 2px solid transparent; background: transparent;
            color: #84746a; font-weight: 500; font-size: 14px; padding: 22px 0px 18px 0px; margin-top: 4px;
        }
        #TopTabBtn:hover { color: #86522b; }
        #TopTabBtn:checked { color: #86522b; border-bottom: 2px solid #86522b; font-weight: 600; }

        #OpenCsvBtn {
            border: 1px solid #c2855a; border-radius: 8px; color: #86522b; font-weight: 700; font-size: 12px; padding: 6px 16px;
        }
        #OpenCsvBtn:hover { background-color: #fdf8f4; }

        #RunEngineBtn {
            background-color: #86522b; border-radius: 8px; color: #ffffff; font-weight: 700; font-size: 12px; padding: 6px 16px;
        }
        #RunEngineBtn:hover { background-color: #754624; }
        #RunEngineBtn:disabled { background-color: #d6c3b7; }

        #PageHeader { background-color: #ffffff; border-bottom: 1px solid #e8e2d9; }
        #PageTitle { color: #1e1b19; font-family: 'Newsreader'; font-size: 20px; text-transform: uppercase; letter-spacing: 1px; }
        #PageSubtitle { color: #84746a; font-size: 12px; }
        #ExportCsvBtn { color: #86522b; font-weight: 700; font-size: 12px; text-transform: uppercase; border: none; background: transparent; }

        #FilterBar { background-color: #faf2ee; }
        QComboBox {
            background-color: #ffffff; border: 1px solid #e8e2d9; border-radius: 12px;
            padding: 4px 12px; color: #52443c; font-weight: 600; font-size: 11px;
        }
        #ResultsLabel { color: #84746a; font-weight: 500; font-size: 11px; }

        #TableWrapper { background-color: #ffffff; border: 1px solid #f0ebe5; border-radius: 12px; }
        QTableWidget { border: none; background-color: transparent; }
        QHeaderView::section {
            background-color: #f5f0ea; color: #d6c3b7; border: none; border-bottom: 1px solid #e8e2d9;
            padding: 12px 16px; font-weight: 700; font-size: 11px; text-transform: uppercase; letter-spacing: 1px;
        }
        QTableWidget::item {
            padding: 8px 16px; border-bottom: 1px solid #e8e2d9;
        }

        #TableSummary { background-color: #ffffff; border-top: 1px solid #e8e2d9; }
        #SummaryLabel { color: #84746a; font-weight: 700; font-size: 10px; text-transform: uppercase; letter-spacing: 1px; }
        #SummaryValue { color: #1e1b19; font-weight: 600; font-family: 'IBM Plex Mono'; font-size: 14px; }

        #BottomBar { background-color: #ffffff; border-top: 1px solid #e8e2d9; }
    )");
}

void MainWindow::setLedState(LedState state) {
    if (engineStateValue_ == nullptr) {
        return;
    }

    if (state == LedState::Running) {
        engineStateValue_->setStyleSheet(
            "font-weight: 700; font-size: 10px; color: #c2855a; letter-spacing: 1px;");
        engineStateValue_->setText("● STATUS RUNNING");
        return;
    }

    if (state == LedState::Error) {
        engineStateValue_->setStyleSheet(
            "font-weight: 700; font-size: 10px; color: #a33b3c; letter-spacing: 1px;");
        engineStateValue_->setText("● STATUS ERROR");
        return;
    }

    engineStateValue_->setStyleSheet(
        "font-weight: 700; font-size: 10px; color: #2c694d; letter-spacing: 1px;");
    engineStateValue_->setText("● STATUS IDLE");
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
            reportsSubtitleLabel_->setText(QString("%1 records from last engine run").arg(totalReports));
        }
    }

    if (ordersProcessedValue_ != nullptr) {
        ordersProcessedValue_->setText(
            QString("list_alt  Orders Processed: %1").arg(QLocale().toString(ordersProcessed_)));
    }
    if (reportsGeneratedValue_ != nullptr) {
        reportsGeneratedValue_->setText(
            QString("description  Reports Generated: %1").arg(QLocale().toString(totalReports)));
    }
    if (lastRunDurationValue_ != nullptr) {
        lastRunDurationValue_->setText(QString("sync  Cycle: %1 ms").arg(lastRunDurationMs_));
    }

    const int shownRows = executionReportsTable_ != nullptr ? executionReportsTable_->rowCount() : totalReports;
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
    const int statusFilterValue = statusFilter_ != nullptr ? statusFilter_->currentData().toInt() : -1;
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

        auto *clientItem =
            new QTableWidgetItem(fixedCharToQString(report.clientOrderId, kClientOrderIdLen));
        auto *orderItem = new QTableWidgetItem(fixedCharToQString(report.orderId, kOrderIdLen));
        auto *instrumentItem = new QTableWidgetItem(instrumentName(report.instrument));
        auto *sideItem = new QTableWidgetItem(sideName(report.side));
        auto *priceItem = new QTableWidgetItem(locale.toString(report.price, 'f', 2));
        auto *qtyItem = new QTableWidgetItem(locale.toString(report.quantity));
        auto *statusItem = new QTableWidgetItem(statusName(report.status));
        auto *reasonItem = new QTableWidgetItem(fixedCharToQString(report.reason, kReasonLen));
        auto *timeItem = new QTableWidgetItem(compactTransactTime(report.transactTime));

        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        qtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sideItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setTextAlignment(Qt::AlignCenter);
        timeItem->setTextAlignment(Qt::AlignCenter);

        const QColor rowBg = rowBackgroundForStatus(report.status);
        const QColor statusColor = rowStatusColor(report.status);

        std::array<QTableWidgetItem *, 9> rowItems = {clientItem, orderItem,   instrumentItem,
                                                       sideItem,   priceItem,   qtyItem,
                                                       statusItem, reasonItem,  timeItem};

        for (QTableWidgetItem *item : rowItems) {
            item->setBackground(rowBg);
            item->setForeground(QColor("#3b312b"));
        }

        sideItem->setBackground(sideBackground(report.side));
        sideItem->setForeground(sideForeground(report.side));

        statusItem->setForeground(statusColor);
        statusItem->setFont(QFont(statusItem->font().family(), statusItem->font().pointSize(),
                                  QFont::DemiBold));

        executionReportsTable_->setItem(row, 0, clientItem);
        executionReportsTable_->setItem(row, 1, orderItem);
        executionReportsTable_->setItem(row, 2, instrumentItem);
        executionReportsTable_->setItem(row, 3, sideItem);
        executionReportsTable_->setItem(row, 4, priceItem);
        executionReportsTable_->setItem(row, 5, qtyItem);
        executionReportsTable_->setItem(row, 6, statusItem);
        executionReportsTable_->setItem(row, 7, reasonItem);
        executionReportsTable_->setItem(row, 8, timeItem);
        executionReportsTable_->setRowHeight(row, 48);
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

    const bool fileOpenError =
        parseResults.size() == 1 && !parseResults.front().ok &&
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
        QMessageBox::critical(this, "Run Engine",
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
