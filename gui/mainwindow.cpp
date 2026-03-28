#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "engine/executionhandler.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include "order_entry_widget.h"
#include "orderbook_widget.h"
#include "performance_dashboard.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui_(new Ui::MainWindow) {
    qRegisterMetaType<Instrument>("Instrument");

    ui_->setupUi(this);

    // Populate topTabButtons_ from .ui widgets
    topTabButtons_ = {ui_->orderBookButton, ui_->reportsButton, ui_->manualButton,
                      ui_->performanceButton};

    // Set object names needed for stylesheet targeting
    ui_->orderBookButton->setObjectName("TopTabBtn");
    ui_->reportsButton->setObjectName("TopTabBtn");
    ui_->manualButton->setObjectName("TopTabBtn");
    ui_->performanceButton->setObjectName("TopTabBtn");
    ui_->openCsvButton->setObjectName("OpenCsvBtn");
    ui_->runEngineButton->setObjectName("RunEngineBtn");
    ui_->exportCsvButton->setObjectName("ExportCsvBtn");
    ui_->reportsSubtitleLabel->setObjectName("PageSubtitle");
    ui_->reportsTitle->setObjectName("PageTitle");
    ui_->orderTitle->setObjectName("PageTitle");
    ui_->showingResultsLabel->setObjectName("ResultsLabel");
    ui_->totLabel->setObjectName("SummaryLabel");
    ui_->summaryTotalValue->setObjectName("SummaryValue");
    ui_->ordersProcessedValue->setObjectName("BottomMetricMuted");
    ui_->reportsGeneratedValue->setObjectName("BottomMetricPositive");
    ui_->lastRunDurationValue->setObjectName("BottomMetricMuted");
    ui_->engineStateValue->setObjectName("BottomMetricState");
    ui_->orderBookPageHeader->setObjectName("PageHeader");
    ui_->reportsPageHeader->setObjectName("PageHeader");
    ui_->navHeader->setStyleSheet("QLabel {"
                                  "  color: #4a3f38;"
                                  "  font-size: 9px;"
                                  "  font-weight: 700;"
                                  "  letter-spacing: 2px;"
                                  "  padding: 0px 18px;"
                                  "  margin-bottom: 4px;"
                                  "}");

    // Divider styles
    ui_->summaryDivider->setStyleSheet(
        "background-color: #e8e2d9; margin-left: 24px; margin-right: 24px;");
    ui_->bottomDivider1->setStyleSheet("background-color: #ddd0c6;");
    ui_->bottomDivider2->setStyleSheet("background-color: #ddd0c6;");

    // Summary value styles
    ui_->summaryFilledValue->setStyleSheet(
        "font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: #2c694d;");
    ui_->summaryPartialValue->setStyleSheet(
        "font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: #c2855a;");
    ui_->summaryRejectedValue->setStyleSheet(
        "font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: #a33b3c;");
    ui_->summaryNewValue->setStyleSheet(
        "font-weight: 600; font-size: 12px; font-family: 'IBM Plex Mono'; color: #5b8fcf;");

    // Populate combo boxes
    ui_->orderBookInstrumentCombo->addItem("Rose", static_cast<int>(Instrument::Rose));
    ui_->orderBookInstrumentCombo->addItem("Lavender", static_cast<int>(Instrument::Lavender));
    ui_->orderBookInstrumentCombo->addItem("Lotus", static_cast<int>(Instrument::Lotus));
    ui_->orderBookInstrumentCombo->addItem("Tulip", static_cast<int>(Instrument::Tulip));
    ui_->orderBookInstrumentCombo->addItem("Orchid", static_cast<int>(Instrument::Orchid));

    ui_->instrumentFilter->addItem("All Instruments", -1);
    ui_->instrumentFilter->addItem("Rose", static_cast<int>(Instrument::Rose));
    ui_->instrumentFilter->addItem("Lavender", static_cast<int>(Instrument::Lavender));
    ui_->instrumentFilter->addItem("Lotus", static_cast<int>(Instrument::Lotus));
    ui_->instrumentFilter->addItem("Tulip", static_cast<int>(Instrument::Tulip));
    ui_->instrumentFilter->addItem("Orchid", static_cast<int>(Instrument::Orchid));

    ui_->statusFilter->addItem("All Status", -1);
    ui_->statusFilter->addItem("New", static_cast<int>(Status::New));
    ui_->statusFilter->addItem("Rejected", static_cast<int>(Status::Rejected));
    ui_->statusFilter->addItem("Fill", static_cast<int>(Status::Fill));
    ui_->statusFilter->addItem("PFill", static_cast<int>(Status::PFill));

    ui_->sideFilter->addItem("All Sides", -1);
    ui_->sideFilter->addItem("Buy", static_cast<int>(Side::Buy));
    ui_->sideFilter->addItem("Sell", static_cast<int>(Side::Sell));

    // Configure executionReportsTable
    ui_->executionReportsTable->setHorizontalHeaderLabels(
        {"Order ID", "Client Order ID", "Instrument", "Side", "Exec Status", "Quantity", "Price"});
    ui_->executionReportsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->executionReportsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui_->executionReportsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui_->executionReportsTable->setShowGrid(false);
    ui_->executionReportsTable->setAlternatingRowColors(false);
    ui_->executionReportsTable->setWordWrap(false);
    ui_->executionReportsTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui_->executionReportsTable->verticalHeader()->setVisible(false);
    ui_->executionReportsTable->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    ui_->executionReportsTable->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    ui_->executionReportsTable->horizontalHeader()->setStretchLastSection(true);
    ui_->executionReportsTable->horizontalHeader()->setHighlightSections(false);
    auto *reportsHeader = ui_->executionReportsTable->horizontalHeader();
    reportsHeader->setMinimumSectionSize(56);
    reportsHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    reportsHeader->setSectionResizeMode(6, QHeaderView::Stretch);

    // Sidebar nav connections
    connect(ui_->sidebarInstrumentsButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageOrderBook); });
    connect(ui_->sidebarAnalyticsButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageExecutionReports); });
    connect(ui_->sidebarManualButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageManualEntry); });
    connect(ui_->sidebarPerformanceButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPagePerformance); });
    connect(ui_->sidebarSupportButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "Support",
                                 "Need help with LillyLedger?\n"
                                 "1. Load an input CSV\n"
                                 "2. Run Engine\n"
                                 "3. Inspect reports, order book and performance.");
    });

    // Top bar tab connections
    connect(ui_->orderBookButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageOrderBook); });
    connect(ui_->reportsButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageExecutionReports); });
    connect(ui_->manualButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPageManualEntry); });
    connect(ui_->performanceButton, &QPushButton::clicked, this,
            [this]() { setCurrentPage(kPagePerformance); });

    // Action buttons
    connect(ui_->openCsvButton, &QPushButton::clicked, this, [this]() {
        const QString filePath =
            QFileDialog::getOpenFileName(this, "Open CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            loadInputFile(filePath);
        }
    });
    connect(ui_->runEngineButton, &QPushButton::clicked, this, &MainWindow::runEngine);
    connect(ui_->exportCsvButton, &QPushButton::clicked, this, [this]() {
        const QString filePath =
            QFileDialog::getSaveFileName(this, "Save CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            exportResults(filePath);
        }
    });

    // Filter connections
    connect(ui_->instrumentFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });
    connect(ui_->statusFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });
    connect(ui_->sideFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateExecutionReportsTable(); });

    // Instrument combo connection
    connect(ui_->orderBookInstrumentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0 || index >= kInstrumentCount) {
                    return;
                }
                emit currentInstrumentChanged(static_cast<Instrument>(index));
            });

    connect(this, &MainWindow::currentInstrumentChanged, this,
            [this](Instrument) { updateOrderBookTab(); });

    // Widget cross-connections
    connect(ui_->manualEntryWidget, &ManualOrderEntryWidget::orderSubmitted, this,
            [this](const Order &order) {
                const auto reports = router_.route(order);
                ordersProcessed_ += 1;
                executionReports_.insert(executionReports_.end(), reports.begin(), reports.end());

                updateExecutionReportsTable();
                updateOrderBookTab();
                refreshUiState();
                setLedState(LedState::Idle);
            });

    connect(ui_->orderBookWidget, &OrderBookWidget::priceClicked, ui_->manualEntryWidget,
            &ManualOrderEntryWidget::prefillPrice);

    applyLightTheme();
    setCurrentPage(kPageExecutionReports);
    refreshUiState();
    setLedState(LedState::Idle);
    updateOrderBookTab();
}

MainWindow::~MainWindow() {
    delete ui_;
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
    if (state == LedState::Running) {
        ui_->engineStateValue->setProperty("engineState", "running");
        ui_->engineStateValue->setText("STATUS RUNNING");
        ui_->engineStateValue->style()->unpolish(ui_->engineStateValue);
        ui_->engineStateValue->style()->polish(ui_->engineStateValue);
        return;
    }

    if (state == LedState::Error) {
        ui_->engineStateValue->setProperty("engineState", "error");
        ui_->engineStateValue->setText("STATUS ERROR");
        ui_->engineStateValue->style()->unpolish(ui_->engineStateValue);
        ui_->engineStateValue->style()->polish(ui_->engineStateValue);
        return;
    }

    ui_->engineStateValue->setProperty("engineState", "idle");
    ui_->engineStateValue->setText("STATUS IDLE");
    ui_->engineStateValue->style()->unpolish(ui_->engineStateValue);
    ui_->engineStateValue->style()->polish(ui_->engineStateValue);
}

void MainWindow::refreshUiState() {
    ui_->runEngineButton->setEnabled(!inputFilePath_.isEmpty());
    ui_->exportCsvButton->setEnabled(!executionReports_.empty());

    const int totalReports = static_cast<int>(executionReports_.size());
    if (totalReports == 0) {
        ui_->reportsSubtitleLabel->setText("No reports yet");
    } else {
        ui_->reportsSubtitleLabel->setText(
            QString("%1 records from last engine run").arg(totalReports));
    }

    ui_->ordersProcessedValue->setText(
        QString("Orders Processed: %1").arg(QLocale().toString(ordersProcessed_)));
    ui_->reportsGeneratedValue->setText(
        QString("Reports Generated: %1").arg(QLocale().toString(totalReports)));
    ui_->lastRunDurationValue->setText(QString("Cycle: %1 ms").arg(lastRunDurationMs_));

    const int shownRows = ui_->executionReportsTable->rowCount();
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

    ui_->summaryTotalValue->setText(QLocale().toString(totalReports));
    ui_->summaryFilledValue->setText(QString("%1 ●").arg(QLocale().toString(fillCount)));
    ui_->summaryPartialValue->setText(QString("%1 ●").arg(QLocale().toString(pfillCount)));
    ui_->summaryRejectedValue->setText(QString("%1 ●").arg(QLocale().toString(rejectedCount)));
    ui_->summaryNewValue->setText(QString("%1 ●").arg(QLocale().toString(newCount)));
    ui_->showingResultsLabel->setText(
        QString("Showing %1 / %2 results").arg(filteredCount).arg(totalReports));
}

void MainWindow::updateExecutionReportsTable() {
    const int instrumentFilterValue = ui_->instrumentFilter->currentData().toInt();
    const int statusFilterValue = ui_->statusFilter->currentData().toInt();
    const int sideFilterValue = ui_->sideFilter->currentData().toInt();

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

    ui_->executionReportsTable->setRowCount(static_cast<int>(filteredReports.size()));

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

        std::array<QTableWidgetItem *, 7> rowItems = {
            orderItem, clientItem, instrumentItem, sideItem, statusItem, qtyItem, priceItem};

        for (QTableWidgetItem *item : rowItems) {
            item->setBackground(rowBg);
            item->setForeground(QColor("#3b312b"));
        }

        sideItem->setBackground(sideBackground(report.side));
        sideItem->setForeground(sideForeground(report.side));

        statusItem->setForeground(statusColor);
        statusItem->setFont(
            QFont(statusItem->font().family(), statusItem->font().pointSize(), QFont::DemiBold));

        ui_->executionReportsTable->setItem(row, 0, orderItem);
        ui_->executionReportsTable->setItem(row, 1, clientItem);
        ui_->executionReportsTable->setItem(row, 2, instrumentItem);
        ui_->executionReportsTable->setItem(row, 3, sideItem);
        ui_->executionReportsTable->setItem(row, 4, statusItem);
        ui_->executionReportsTable->setItem(row, 5, qtyItem);
        ui_->executionReportsTable->setItem(row, 6, priceItem);
    }

    updateSummaryBar(static_cast<int>(filteredReports.size()));
}

void MainWindow::updateOrderBookTab() {
    const Instrument instrument = selectedInstrument();
    std::vector<BookEntry> buys;
    std::vector<BookEntry> sells;
    buildSyntheticBookFromReports(executionReports_, instrument, buys, sells);
    ui_->orderBookWidget->updateBook(buys, sells, instrument);
}

void MainWindow::updatePerformanceTab(qint64 parseUs, qint64 matchUs, qint64 writeUs) {
    PerfStats stats;
    stats.parseUs = parseUs;
    stats.matchUs = matchUs;
    stats.writeUs = writeUs;
    stats.totalUs = parseUs + matchUs + writeUs;
    stats.orderCount = static_cast<int>(ordersProcessed_);
    stats.reportCount = static_cast<int>(executionReports_.size());

    ui_->performanceDashboard->addRunHistory(stats, executionReports_);
}

void MainWindow::setCurrentPage(int index) {
    if (index < 0 || index >= ui_->stackedWidget->count()) {
        return;
    }

    ui_->stackedWidget->setCurrentIndex(index);
    for (std::size_t i = 0; i < topTabButtons_.size(); ++i) {
        if (topTabButtons_[i] != nullptr) {
            topTabButtons_[i]->setChecked(static_cast<int>(i) == index);
        }
    }

    ui_->sidebarInstrumentsButton->setChecked(index == kPageOrderBook);
    ui_->sidebarAnalyticsButton->setChecked(index == kPageExecutionReports);
    ui_->sidebarManualButton->setChecked(index == kPageManualEntry);
    ui_->sidebarPerformanceButton->setChecked(index == kPagePerformance);
    ui_->sidebarSupportButton->setChecked(false);

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
    ui_->reportsSubtitleLabel->setText(QString("Input loaded: %1").arg(info.fileName()));
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
    const int index = ui_->orderBookInstrumentCombo->currentIndex();
    if (index >= 0 && index < kInstrumentCount) {
        return static_cast<Instrument>(index);
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
