#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "engine/executionhandler.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include "order_entry_widget.h"
#include "orderbook_widget.h"
#include "performance_dashboard.h"
#include <QAbstractItemView>
#include <QAbstractTableModel>
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

// ── Report field helpers ─────────────────────────────────────────────────────

QString fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}

QString instrumentName(Instrument instrument) {
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

QString sideName(Side side) {
    switch (side) {
    case Side::Buy:
        return "BUY";
    case Side::Sell:
        return "SELL";
    }
    return "Unknown";
}

QString statusName(Status status) {
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

QString compactTransactTime(const char *text) {
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

// ── Row / cell colour helpers ────────────────────────────────────────────────

QColor rowBackgroundForStatus(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#faf2ee"); // surface-container-low
    case Status::Fill:
        return QColor("#f4ece8"); // surface-container
    case Status::PFill:
        return QColor("#ffdcc6"); // primary-fixed
    case Status::Rejected:
        return QColor("#ffdad6"); // error-container
    }
    return QColor("#ffffff");
}

QColor rowStatusColor(Status status) {
    switch (status) {
    case Status::New:
        return QColor("#84746a"); // outline
    case Status::Fill:
        return QColor("#2c694d"); // secondary
    case Status::PFill:
        return QColor("#86522b"); // primary
    case Status::Rejected:
        return QColor("#a33b3c"); // tertiary
    }
    return QColor("#52443c");
}

QColor sideBackground(Side side) {
    return side == Side::Buy ? QColor("#b0f1cc")
                             : QColor("#ffdad6"); // secondary-container / error-container
}

QColor sideForeground(Side side) {
    return side == Side::Buy ? QColor("#2c694d") : QColor("#a33b3c"); // secondary / tertiary
}

// ── Virtual model for the execution-reports table ────────────────────────────
// Stores only pointers — zero per-cell allocations. Only visible rows are
// queried by the view, so memory and CPU scale with viewport height, not data size.

class ExecutionReportTableModel : public QAbstractTableModel {
  public:
    static constexpr int kColCount = 7;

    explicit ExecutionReportTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    void setReports(std::vector<const ExecutionReport *> rows) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : kColCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
            section >= kColCount) {
            return {};
        }
        static const std::array<const char *, kColCount> kHeaders = {
            "Order ID",    "Client Order ID", "Instrument", "Side",
            "Exec Status", "Quantity",        "Price"};
        return QString(kHeaders[section]);
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) {
            return {};
        }
        const ExecutionReport &r = *rows_[static_cast<std::size_t>(index.row())];
        const int col = index.column();

        switch (role) {
        case Qt::DisplayRole:
            return displayData(r, col);
        case Qt::TextAlignmentRole:
            if (col == 3 || col == 4) {
                return static_cast<int>(Qt::AlignCenter);
            }
            if (col == 5 || col == 6) {
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            }
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        case Qt::BackgroundRole:
            if (col == 3) {
                return sideBackground(r.side);
            }
            return rowBackgroundForStatus(r.status);
        case Qt::ForegroundRole:
            if (col == 3) {
                return sideForeground(r.side);
            }
            if (col == 4) {
                return rowStatusColor(r.status);
            }
            return QColor("#3b312b");
        case Qt::FontRole:
            if (col == 4) {
                QFont f;
                f.setWeight(QFont::DemiBold);
                return f;
            }
            return {};
        default:
            return {};
        }
    }

  private:
    static QVariant displayData(const ExecutionReport &r, int col) {
        switch (col) {
        case 0:
            return fixedCharToQString(r.orderId, kOrderIdLen);
        case 1:
            return fixedCharToQString(r.clientOrderId, kClientOrderIdLen);
        case 2:
            return instrumentName(r.instrument);
        case 3:
            return sideName(r.side);
        case 4:
            return statusName(r.status);
        case 5:
            return QLocale().toString(r.quantity);
        case 6:
            return QLocale().toString(r.price, 'f', 2);
        default:
            return {};
        }
    }

    std::vector<const ExecutionReport *> rows_;
};

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

    // Set object names needed for stylesheet targeting
    ui_->openCsvButton->setObjectName("OpenCsvBtn");
    ui_->runEngineButton->setObjectName("RunEngineBtn");
    ui_->exportCsvButton->setObjectName("ExportCsvBtn");
    ui_->reportsSubtitleLabel->setObjectName("PageSubtitle");
    ui_->reportsTitle->setObjectName("PageTitle");
    ui_->orderTitle->setObjectName("PageTitle");
    ui_->orderBookSubtitleLabel->setObjectName("PageSubtitle");
    ui_->manualEntryTitle->setObjectName("PageTitle");
    ui_->manualEntrySubtitleLabel->setObjectName("PageSubtitle");
    ui_->performanceTitle->setObjectName("PageTitle");
    ui_->performanceSubtitleLabel->setObjectName("PageSubtitle");
    ui_->showingResultsLabel->setObjectName("ResultsLabel");
    ui_->totLabel->setObjectName("SummaryLabel");
    ui_->summaryTotalValue->setObjectName("SummaryValue");
    ui_->ordersProcessedValue->setObjectName("BottomMetricMuted");
    ui_->reportsGeneratedValue->setObjectName("BottomMetricPositive");
    ui_->lastRunDurationValue->setObjectName("BottomMetricMuted");
    ui_->engineStateValue->setObjectName("BottomMetricState");
    ui_->orderBookPageHeader->setObjectName("PageHeader");
    ui_->reportsPageHeader->setObjectName("PageHeader");
    ui_->manualEntryPageHeader->setObjectName("PageHeader");
    ui_->performancePageHeader->setObjectName("PageHeader");
    ui_->navHeader->setStyleSheet("QLabel {"
                                  "  color: #52443c;"
                                  "  font-size: 9px;"
                                  "  font-weight: 700;"
                                  "  letter-spacing: 2.5px;"
                                  "  padding: 0px 18px;"
                                  "  margin-bottom: 4px;"
                                  "  text-transform: uppercase;"
                                  "}");

    // Divider styles
    ui_->summaryDivider->setStyleSheet(
        "background-color: #d6c3b7; margin-left: 24px; margin-right: 24px;");
    ui_->bottomDivider1->setStyleSheet("background-color: #d6c3b7;");
    ui_->bottomDivider2->setStyleSheet("background-color: #d6c3b7;");

    // Summary value styles
    ui_->summaryFilledValue->setStyleSheet("font-weight: 700; font-size: 12px; font-family: "
                                           "'Courier New', monospace; color: #2c694d;");
    ui_->summaryPartialValue->setStyleSheet("font-weight: 700; font-size: 12px; font-family: "
                                            "'Courier New', monospace; color: #86522b;");
    ui_->summaryRejectedValue->setStyleSheet("font-weight: 700; font-size: 12px; font-family: "
                                             "'Courier New', monospace; color: #a33b3c;");
    ui_->summaryNewValue->setStyleSheet("font-weight: 700; font-size: 12px; font-family: 'Courier "
                                        "New', monospace; color: #84746a;");

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

    // Configure executionReportsTable (QTableView + virtual model)
    auto *model = new ExecutionReportTableModel(this);
    reportsModel_ = model;
    ui_->executionReportsTable->setModel(model);
    ui_->executionReportsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->executionReportsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui_->executionReportsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui_->executionReportsTable->setShowGrid(false);
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

/* ══════════════════════════════════════════════════════════════════════
   LillyLedger — Qt stylesheet
   Palette lifted verbatim from the HTML reference design.

   Token map (HTML → QSS hex):
     background / surface              #fff8f5
     surface-container-lowest          #ffffff
     surface-container-low             #faf2ee
     surface-container                 #f4ece8
     surface-container-high            #eee7e3
     surface-container-highest         #e9e1dd
     surface-dim                       #e0d8d5
     surface-variant                   #e9e1dd
     on-surface                        #1e1b19
     on-surface-variant                #52443c
     outline                           #84746a
     outline-variant                   #d6c3b7
     primary                           #86522b
     primary-container                 #c2855a
     primary-fixed                     #ffdcc6
     primary-fixed-dim                 #fcb889
     on-primary                        #ffffff
     on-primary-container              #482100
     secondary                         #2c694d
     secondary-container               #b0f1cc
     on-secondary-container            #327052
     tertiary                          #a33b3c
     error                             #ba1a1a
     inverse-surface (sidebar)         #33302d  ← darkest panel
     sidebar bg                        #1c1917  ← near-black brown

   Shape scale (from HTML tailwind config):
     DEFAULT  2px   ← nearly square
     lg       4px   ← subtle round
     xl       8px   ← inputs, buttons
     full    12px   ← pills / toggles
   ══════════════════════════════════════════════════════════════════════ */

/* ── Reset & base ──────────────────────────────────────────────────── */
QMainWindow, QDialog { background-color: #fff8f5; }

QLabel      { font-size: 13px; color: #1e1b19; font-family: 'Segoe UI', 'Helvetica Neue', sans-serif; }
QPushButton { font-size: 13px; font-family: 'Segoe UI', 'Helvetica Neue', sans-serif; min-height: 36px; }
QComboBox   { font-size: 13px; font-family: 'Segoe UI', 'Helvetica Neue', sans-serif; min-height: 36px; }
QLineEdit   { font-size: 13px; font-family: 'Segoe UI', 'Helvetica Neue', sans-serif; min-height: 36px; }
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
    letter-spacing: 0.5px;
}
#LogoSubtitle {
    color: #52443c;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 3px;
    text-transform: uppercase;
}

QPushButton[navIcon="true"] {
    text-align: left;
    padding: 11px 18px;
    border: none;
    border-radius: 0px;
    color: #84746a;
    font-weight: 500;
    font-size: 13px;
    background-color: transparent;
    min-height: 44px;
}
QPushButton[navIcon="true"]:hover {
    background-color: #33302d;
    color: #e9e1dd;
}
QPushButton[navIcon="true"]:checked {
    color: #ffffff;
    font-weight: 700;
    background-color: #33302d;
    border-left: 2px solid #c2855a;
    padding-left: 16px;
}

/* ── Top bar ───────────────────────────────────────────────────────── */
#TopBar {
    background-color: #fff8f5;
    border-bottom: 1px solid #d6c3b7;
}

/* ── Open CSV / Run Engine buttons ─────────────────────────────────── */
#OpenCsvBtn {
    border: 1.5px solid #c2855a;
    border-radius: 8px;
    color: #86522b;
    background-color: transparent;
    font-weight: 700;
    font-size: 12px;
    letter-spacing: 0.5px;
    padding: 0px 18px;
    min-height: 34px;
    min-width: 90px;
}
#OpenCsvBtn:hover  { background-color: #ffdcc6; }
#OpenCsvBtn:pressed { background-color: #fcb889; }

#RunEngineBtn {
    background-color: #86522b;
    border: none;
    border-radius: 8px;
    color: #ffffff;
    font-weight: 700;
    font-size: 12px;
    letter-spacing: 0.5px;
    padding: 0px 18px;
    min-height: 34px;
    min-width: 100px;
}
#RunEngineBtn:hover    { background-color: #6a3b16; }
#RunEngineBtn:pressed  { background-color: #482100; }
#RunEngineBtn:disabled { background-color: #e0d8d5; color: #84746a; }

/* ── Page headers ──────────────────────────────────────────────────── */
#PageHeader {
    background-color: #fff8f5;
    border-bottom: 1px solid #d6c3b7;
}
#PageTitle {
    color: #1e1b19;
    font-family: 'Georgia', 'Palatino Linotype', serif;
    font-size: 18px;
    font-weight: 400;
    letter-spacing: 2px;
}
#PageSubtitle {
    color: #84746a;
    font-size: 11px;
    font-weight: 400;
    letter-spacing: 0.5px;
}
#ExportCsvBtn {
    color: #86522b;
    font-weight: 700;
    font-size: 12px;
    letter-spacing: 0.5px;
    border: 1.5px solid #c2855a;
    border-radius: 8px;
    background: transparent;
    padding: 0px 14px;
    min-height: 34px;
}
#ExportCsvBtn:hover    { background-color: #ffdcc6; }
#ExportCsvBtn:disabled { color: #d6c3b7; border-color: #e0d8d5; }

/* ── Filter bar ────────────────────────────────────────────────────── */
#FilterBar {
    background-color: #f4ece8;
    border-bottom: 1px solid #d6c3b7;
}

QComboBox {
    background-color: #ffffff;
    border: 1px solid #d6c3b7;
    border-radius: 4px;
    padding: 0px 10px;
    color: #1e1b19;
    font-weight: 500;
    font-size: 12px;
    min-height: 34px;
}
QComboBox:hover { border-color: #86522b; }
QComboBox:focus { border-color: #86522b; }
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
    color: #1e1b19;
    border: 1px solid #d6c3b7;
    border-radius: 2px;
    selection-background-color: #f4ece8;
    selection-color: #1e1b19;
    outline: 0px;
    padding: 4px;
}

#ResultsLabel {
    color: #84746a;
    font-weight: 500;
    font-size: 11px;
    letter-spacing: 0.3px;
}

/* ── Tables ────────────────────────────────────────────────────────── */
#TableWrapper {
    background-color: #ffffff;
    border: 1px solid #d6c3b7;
    border-radius: 4px;
}
#executionReportsTable {
    border: none;
    background-color: transparent;
    gridline-color: #eee7e3;
    font-size: 13px;
    color: #1e1b19;
    font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;
}
#executionReportsTable::item {
    padding: 4px 12px;
    border-bottom: 1px solid #eee7e3;
}
#executionReportsTable::item:selected {
    background-color: #ffdcc6;
    color: #1e1b19;
}
#executionReportsTable QHeaderView::section {
    background-color: #f4ece8;
    color: #52443c;
    border: none;
    border-bottom: 1px solid #d6c3b7;
    border-right: 1px solid #d6c3b7;
    padding: 8px 12px;
    font-weight: 700;
    font-size: 10px;
    letter-spacing: 1px;
    font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;
    text-transform: uppercase;
}
#executionReportsTable QHeaderView::section:last { border-right: none; }

/* ── Summary bar ───────────────────────────────────────────────────── */
#TableSummary {
    background-color: #faf2ee;
    border-top: 1px solid #d6c3b7;
}
#SummaryLabel {
    color: #84746a;
    font-weight: 700;
    font-size: 10px;
    letter-spacing: 1.5px;
    text-transform: uppercase;
}
#SummaryValue {
    color: #1e1b19;
    font-weight: 700;
    font-family: 'Courier New', 'Consolas', monospace;
    font-size: 14px;
}

/* ── Scrollbars ─────────────────────────────────────────────────────── */
QScrollBar:vertical {
    background: #faf2ee;
    width: 6px;
    margin: 0;
    border-radius: 3px;
}
QScrollBar::handle:vertical {
    background: #d6c3b7;
    border-radius: 3px;
    min-height: 32px;
}
QScrollBar::handle:vertical:hover { background: #c2855a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

QScrollBar:horizontal {
    background: #faf2ee;
    height: 6px;
    margin: 0;
    border-radius: 3px;
}
QScrollBar::handle:horizontal {
    background: #d6c3b7;
    border-radius: 3px;
    min-width: 32px;
}
QScrollBar::handle:horizontal:hover { background: #c2855a; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }

/* ── Input fields ──────────────────────────────────────────────────── */
QLineEdit {
    background-color: #ffffff;
    border: 1px solid #d6c3b7;
    border-radius: 8px;
    padding: 0px 12px;
    color: #1e1b19;
    selection-background-color: #ffdcc6;
}
QLineEdit:hover { border-color: #c2855a; }
QLineEdit:focus { border-color: #86522b; border-width: 1.5px; }

QSpinBox, QDoubleSpinBox {
    background-color: #ffffff;
    border: 1px solid #d6c3b7;
    border-radius: 8px;
    padding: 0px 8px;
    color: #1e1b19;
}
QSpinBox:hover, QDoubleSpinBox:hover   { border-color: #c2855a; }
QSpinBox:focus, QDoubleSpinBox:focus   { border-color: #86522b; border-width: 1.5px; }
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    width: 20px;
    border: none;
    background: transparent;
}

/* ── QGroupBox (used in Manual Entry) ─────────────────────────────── */
QGroupBox {
    background-color: #ffffff;
    border: 1px solid #d6c3b7;
    border-radius: 4px;
    margin-top: 14px;
    padding: 16px 20px 20px 20px;
    font-size: 10px;
    font-weight: 700;
    color: #84746a;
    letter-spacing: 1.5px;
    text-transform: uppercase;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 16px;
    top: 0px;
    padding: 0px 6px;
    background-color: #fff8f5;
    color: #84746a;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 2px;
    text-transform: uppercase;
}

/* ── Bottom status bar ─────────────────────────────────────────────── */
#BottomBar {
    background-color: #fff8f5;
    border-top: 1px solid #d6c3b7;
}
#BottomMetricMuted, #BottomMetricPositive, #BottomMetricState {
    font-weight: 600;
    font-size: 10px;
    letter-spacing: 1px;
    font-family: 'Segoe UI', 'Helvetica Neue', sans-serif;
    text-transform: uppercase;
}
#BottomMetricMuted    { color: #84746a; }
#BottomMetricPositive { color: #2c694d; }
#BottomMetricState[engineState="idle"]    { color: #2c694d; }
#BottomMetricState[engineState="running"] { color: #86522b; }
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

    updateSummaryBar(reportsModel_->rowCount());
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

    const int filteredCount = static_cast<int>(filteredReports.size());
    static_cast<ExecutionReportTableModel *>(reportsModel_)->setReports(std::move(filteredReports));
    updateSummaryBar(filteredCount);
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