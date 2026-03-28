#include "mainwindow.h"

#include "engine/executionhandler.h"
#include "engine/orderrouter.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <chrono>
#include <vector>

namespace {
constexpr int kInstrumentDockWidth = 220;
constexpr int kStatusDockHeight = 120;
constexpr int kInstrumentCount = 5;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), lightPalette_(QApplication::palette()) {
    qRegisterMetaType<Instrument>("Instrument");

    setWindowTitle("LillyLedger");
    resize(1280, 820);

    setupCentralTabs();
    setupDocks();
    setupMenuBar();
    setupToolBar();

    connect(this, &MainWindow::currentInstrumentChanged, this,
            [this](Instrument) { updateOrderBookTab(); });

    applyDarkTheme();
    refreshUiState();
    setLedState(LedState::Idle);

    if (instrumentList_ != nullptr) {
        instrumentList_->setCurrentRow(0);
    }
}

void MainWindow::setupCentralTabs() {
    tabWidget_ = new QTabWidget(this);
    setCentralWidget(tabWidget_);

    auto *orderBookTab = new QWidget(tabWidget_);
    auto *orderBookLayout = new QVBoxLayout(orderBookTab);
    auto *orderBookHint =
        new QLabel("Instrument summary derived from execution reports.", orderBookTab);
    orderBookHint->setWordWrap(true);
    orderBookSummary_ = new QPlainTextEdit(orderBookTab);
    orderBookSummary_->setReadOnly(true);
    orderBookSummary_->setLineWrapMode(QPlainTextEdit::NoWrap);
    orderBookLayout->addWidget(orderBookHint);
    orderBookLayout->addWidget(orderBookSummary_);
    tabWidget_->addTab(orderBookTab, "Order Book");

    auto *reportsTab = new QWidget(tabWidget_);
    auto *reportsLayout = new QVBoxLayout(reportsTab);
    executionReportsTable_ = new QTableWidget(reportsTab);
    executionReportsTable_->setColumnCount(9);
    executionReportsTable_->setHorizontalHeaderLabels(
        {"Client Order ID", "Order ID", "Instrument", "Side", "Price", "Quantity", "Status",
         "Reason", "Transact Time"});
    executionReportsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    executionReportsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    executionReportsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    executionReportsTable_->horizontalHeader()->setStretchLastSection(true);
    reportsLayout->addWidget(executionReportsTable_);
    tabWidget_->addTab(reportsTab, "Execution Reports");

    auto *manualEntryTab = new QWidget(tabWidget_);
    auto *manualLayout = new QVBoxLayout(manualEntryTab);
    auto *manualHint = new QLabel(
        "Manual entry panel placeholder.\n"
        "GUI stays orchestration-only and does not add matching logic.",
        manualEntryTab);
    manualHint->setWordWrap(true);
    auto *manualSubmitButton = new QPushButton("Submit Order", manualEntryTab);
    manualSubmitButton->setEnabled(false);
    manualLayout->addWidget(manualHint);
    manualLayout->addStretch();
    manualLayout->addWidget(manualSubmitButton);
    tabWidget_->addTab(manualEntryTab, "Manual Entry");

    auto *performanceTab = new QWidget(tabWidget_);
    auto *performanceLayout = new QGridLayout(performanceTab);
    performanceLayout->addWidget(new QLabel("Orders Processed", performanceTab), 0, 0);
    perfOrdersValue_ = new QLabel("0", performanceTab);
    performanceLayout->addWidget(perfOrdersValue_, 0, 1);
    performanceLayout->addWidget(new QLabel("Reports Generated", performanceTab), 1, 0);
    perfReportsValue_ = new QLabel("0", performanceTab);
    performanceLayout->addWidget(perfReportsValue_, 1, 1);
    performanceLayout->addWidget(new QLabel("Last Run Duration", performanceTab), 2, 0);
    perfDurationValue_ = new QLabel("0 ms", performanceTab);
    performanceLayout->addWidget(perfDurationValue_, 2, 1);
    performanceLayout->setColumnStretch(2, 1);
    tabWidget_->addTab(performanceTab, "Performance");
}

void MainWindow::setupDocks() {
    instrumentDock_ = new QDockWidget("Instruments", this);
    instrumentDock_->setAllowedAreas(Qt::LeftDockWidgetArea);
    instrumentDock_->setMinimumWidth(kInstrumentDockWidth);
    instrumentDock_->setMaximumWidth(kInstrumentDockWidth);

    auto *instrumentPanel = new QWidget(instrumentDock_);
    instrumentPanel->setFixedWidth(kInstrumentDockWidth);
    auto *instrumentLayout = new QVBoxLayout(instrumentPanel);
    instrumentList_ = new QListWidget(instrumentPanel);
    instrumentList_->addItems({"🌹 Rose", "💜 Lavender", "🪷 Lotus", "🌷 Tulip", "🌸 Orchid"});
    instrumentLayout->addWidget(instrumentList_);
    instrumentDock_->setWidget(instrumentPanel);
    addDockWidget(Qt::LeftDockWidgetArea, instrumentDock_);

    connect(instrumentList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= kInstrumentCount) {
            return;
        }
        emit currentInstrumentChanged(static_cast<Instrument>(row));
    });

    statusDock_ = new QDockWidget("Engine Status", this);
    statusDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    statusDock_->setMinimumHeight(kStatusDockHeight);
    statusDock_->setMaximumHeight(kStatusDockHeight);

    auto *statusPanel = new QWidget(statusDock_);
    statusPanel->setFixedHeight(kStatusDockHeight);
    auto *statusLayout = new QGridLayout(statusPanel);
    statusLayout->addWidget(new QLabel("Orders Processed", statusPanel), 0, 0);
    ordersProcessedValue_ = new QLabel("0", statusPanel);
    statusLayout->addWidget(ordersProcessedValue_, 0, 1);
    statusLayout->addWidget(new QLabel("Reports Generated", statusPanel), 0, 2);
    reportsGeneratedValue_ = new QLabel("0", statusPanel);
    statusLayout->addWidget(reportsGeneratedValue_, 0, 3);

    statusLayout->addWidget(new QLabel("Last Run Duration", statusPanel), 1, 0);
    lastRunDurationValue_ = new QLabel("0 ms", statusPanel);
    statusLayout->addWidget(lastRunDurationValue_, 1, 1);

    statusLayout->addWidget(new QLabel("Engine LED", statusPanel), 1, 2);
    ledIndicator_ = new QLabel(statusPanel);
    ledIndicator_->setFixedSize(16, 16);
    statusLayout->addWidget(ledIndicator_, 1, 3);
    engineStateValue_ = new QLabel("Idle", statusPanel);
    statusLayout->addWidget(engineStateValue_, 1, 4);
    statusLayout->setColumnStretch(5, 1);

    statusDock_->setWidget(statusPanel);
    addDockWidget(Qt::BottomDockWidgetArea, statusDock_);
}

void MainWindow::setupMenuBar() {
    auto *fileMenu = menuBar()->addMenu("File");
    auto *openInputAction = fileMenu->addAction("Open Input CSV");
    connect(openInputAction, &QAction::triggered, this, [this]() {
        const QString filePath =
            QFileDialog::getOpenFileName(this, "Open Input CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            loadInputFile(filePath);
        }
    });

    auto *saveOutputAction = fileMenu->addAction("Save Output CSV");
    connect(saveOutputAction, &QAction::triggered, this, [this]() {
        const QString filePath =
            QFileDialog::getSaveFileName(this, "Save Output CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            exportResults(filePath);
        }
    });

    fileMenu->addSeparator();
    auto *exitAction = fileMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu("View");
    toggleDockPanelsAction_ = viewMenu->addAction("Toggle All Dock Panels");
    toggleDockPanelsAction_->setCheckable(true);
    toggleDockPanelsAction_->setChecked(true);
    connect(toggleDockPanelsAction_, &QAction::toggled, this, [this](bool visible) {
        if (instrumentDock_ != nullptr) {
            instrumentDock_->setVisible(visible);
        }
        if (statusDock_ != nullptr) {
            statusDock_->setVisible(visible);
        }
    });

    auto syncDockAction = [this]() {
        if (toggleDockPanelsAction_ == nullptr || instrumentDock_ == nullptr || statusDock_ == nullptr) {
            return;
        }
        const bool bothVisible = instrumentDock_->isVisible() && statusDock_->isVisible();
        QSignalBlocker blocker(toggleDockPanelsAction_);
        toggleDockPanelsAction_->setChecked(bothVisible);
    };

    connect(instrumentDock_, &QDockWidget::visibilityChanged, this,
            [syncDockAction](bool) { syncDockAction(); });
    connect(statusDock_, &QDockWidget::visibilityChanged, this,
            [syncDockAction](bool) { syncDockAction(); });

    auto *helpMenu = menuBar()->addMenu("Help");
    auto *aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About LillyLedger",
                           "LillyLedger GUI\nQt6 front-end for the flower commodity engine.");
    });
}

void MainWindow::setupToolBar() {
    auto *toolBar = addToolBar("Main");
    toolBar->setMovable(false);

    auto *openCsvAction = toolBar->addAction("Open CSV");
    connect(openCsvAction, &QAction::triggered, this, [this]() {
        const QString filePath =
            QFileDialog::getOpenFileName(this, "Open Input CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            loadInputFile(filePath);
        }
    });

    runEngineAction_ = toolBar->addAction("Run Engine");
    runEngineAction_->setEnabled(false);
    connect(runEngineAction_, &QAction::triggered, this, &MainWindow::runEngine);

    exportResultsAction_ = toolBar->addAction("Export Results");
    exportResultsAction_->setEnabled(false);
    connect(exportResultsAction_, &QAction::triggered, this, [this]() {
        const QString filePath =
            QFileDialog::getSaveFileName(this, "Save Output CSV", {}, "CSV Files (*.csv)");
        if (!filePath.isEmpty()) {
            exportResults(filePath);
        }
    });

    toolBar->addSeparator();
    toggleThemeAction_ = toolBar->addAction("Theme");
    toggleThemeAction_->setCheckable(true);
    toggleThemeAction_->setChecked(true);
    connect(toggleThemeAction_, &QAction::toggled, this, [this](bool darkEnabled) {
        if (darkEnabled) {
            applyDarkTheme();
            return;
        }
        applyLightTheme();
    });
}

void MainWindow::applyDarkTheme() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1e1e2e"));
    palette.setColor(QPalette::WindowText, QColor("#cdd6f4"));
    palette.setColor(QPalette::Base, QColor("#2a2a3e"));
    palette.setColor(QPalette::AlternateBase, QColor("#1e1e2e"));
    palette.setColor(QPalette::ToolTipBase, QColor("#2a2a3e"));
    palette.setColor(QPalette::ToolTipText, QColor("#cdd6f4"));
    palette.setColor(QPalette::Text, QColor("#cdd6f4"));
    palette.setColor(QPalette::Button, QColor("#2a2a3e"));
    palette.setColor(QPalette::ButtonText, QColor("#cdd6f4"));
    palette.setColor(QPalette::BrightText, QColor("#f38ba8"));
    palette.setColor(QPalette::Highlight, QColor("#89b4fa"));
    palette.setColor(QPalette::HighlightedText, QColor("#1e1e2e"));

    QApplication::setPalette(palette);
    darkThemeEnabled_ = true;
}

void MainWindow::applyLightTheme() {
    QApplication::setPalette(lightPalette_);
    darkThemeEnabled_ = false;
}

void MainWindow::setLedState(LedState state) {
    QString color = "#a6e3a1";
    QString label = "Idle";

    if (state == LedState::Running) {
        color = "#f9e2af";
        label = "Running";
    } else if (state == LedState::Error) {
        color = "#f38ba8";
        label = "Error";
    }

    if (ledIndicator_ != nullptr) {
        ledIndicator_->setStyleSheet(
            QString("QLabel { background-color: %1; border: 1px solid #6c7086; border-radius: 8px; }")
                .arg(color));
    }

    if (engineStateValue_ != nullptr) {
        engineStateValue_->setText(label);
    }
}

void MainWindow::refreshUiState() {
    if (runEngineAction_ != nullptr) {
        runEngineAction_->setEnabled(!inputFilePath_.isEmpty());
    }
    if (exportResultsAction_ != nullptr) {
        exportResultsAction_->setEnabled(!executionReports_.empty());
    }

    const QString ordersText = QString::number(ordersProcessed_);
    const QString reportsText = QString::number(executionReports_.size());
    const QString durationText = QString("%1 ms").arg(lastRunDurationMs_);

    if (ordersProcessedValue_ != nullptr) {
        ordersProcessedValue_->setText(ordersText);
    }
    if (reportsGeneratedValue_ != nullptr) {
        reportsGeneratedValue_->setText(reportsText);
    }
    if (lastRunDurationValue_ != nullptr) {
        lastRunDurationValue_->setText(durationText);
    }

    if (perfOrdersValue_ != nullptr) {
        perfOrdersValue_->setText(ordersText);
    }
    if (perfReportsValue_ != nullptr) {
        perfReportsValue_->setText(reportsText);
    }
    if (perfDurationValue_ != nullptr) {
        perfDurationValue_->setText(durationText);
    }
}

void MainWindow::updateExecutionReportsTable() {
    if (executionReportsTable_ == nullptr) {
        return;
    }

    executionReportsTable_->setRowCount(static_cast<int>(executionReports_.size()));

    for (int row = 0; row < static_cast<int>(executionReports_.size()); ++row) {
        const ExecutionReport &report = executionReports_[static_cast<std::size_t>(row)];
        executionReportsTable_->setItem(
            row, 0,
            new QTableWidgetItem(fixedCharToQString(report.clientOrderId, kClientOrderIdLen)));
        executionReportsTable_->setItem(
            row, 1, new QTableWidgetItem(fixedCharToQString(report.orderId, kOrderIdLen)));
        executionReportsTable_->setItem(row, 2, new QTableWidgetItem(instrumentName(report.instrument)));
        executionReportsTable_->setItem(row, 3, new QTableWidgetItem(sideName(report.side)));
        executionReportsTable_->setItem(row, 4,
                                        new QTableWidgetItem(QString::number(report.price, 'f', 2)));
        executionReportsTable_->setItem(row, 5, new QTableWidgetItem(QString::number(report.quantity)));
        executionReportsTable_->setItem(row, 6, new QTableWidgetItem(statusName(report.status)));
        executionReportsTable_->setItem(
            row, 7, new QTableWidgetItem(fixedCharToQString(report.reason, kReasonLen)));
        executionReportsTable_->setItem(
            row, 8, new QTableWidgetItem(fixedCharToQString(report.transactTime, kTransactTimeLen)));
    }
}

void MainWindow::updateOrderBookTab() {
    if (orderBookSummary_ == nullptr) {
        return;
    }

    const Instrument instrument = selectedInstrument();
    int newReports = 0;
    int fillReports = 0;
    int pfillReports = 0;
    int rejectedReports = 0;
    double tradedNotional = 0.0;
    uint64_t tradedQty = 0;

    for (const ExecutionReport &report : executionReports_) {
        if (report.status == Status::Rejected) {
            ++rejectedReports;
            continue;
        }
        if (report.instrument != instrument) {
            continue;
        }

        if (report.status == Status::New) {
            ++newReports;
        } else if (report.status == Status::Fill) {
            ++fillReports;
            tradedNotional += report.price * static_cast<double>(report.quantity);
            tradedQty += report.quantity;
        } else if (report.status == Status::PFill) {
            ++pfillReports;
            tradedNotional += report.price * static_cast<double>(report.quantity);
            tradedQty += report.quantity;
        }
    }

    const QString summary = QString("Instrument: %1\n"
                                    "Input File: %2\n\n"
                                    "New reports: %3\n"
                                    "Fill reports: %4\n"
                                    "Partial fill reports: %5\n"
                                    "Rejected reports (all instruments): %6\n"
                                    "Matched quantity: %7\n"
                                    "Matched notional: %8")
                                .arg(instrumentName(instrument))
                                .arg(inputFilePath_.isEmpty() ? "(none)" : inputFilePath_)
                                .arg(newReports)
                                .arg(fillReports)
                                .arg(pfillReports)
                                .arg(rejectedReports)
                                .arg(tradedQty)
                                .arg(QString::number(tradedNotional, 'f', 2));

    orderBookSummary_->setPlainText(summary);
}

void MainWindow::loadInputFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    inputFilePath_ = filePath;
    executionReports_.clear();
    ordersProcessed_ = 0;
    lastRunDurationMs_ = 0;

    updateExecutionReportsTable();
    updateOrderBookTab();
    refreshUiState();
    setLedState(LedState::Idle);

    statusBar()->showMessage(QString("Loaded input CSV: %1").arg(filePath), 4000);
}

void MainWindow::runEngine() {
    if (inputFilePath_.isEmpty()) {
        QMessageBox::warning(this, "Run Engine", "Load an input CSV file first.");
        setLedState(LedState::Error);
        return;
    }

    setLedState(LedState::Running);
    statusBar()->showMessage("Running engine...");

    const auto start = std::chrono::steady_clock::now();

    CSVReader reader(inputFilePath_.toStdString());
    const std::vector<ParseResult> parseResults = reader.readAll();

    OrderRouter router;
    std::vector<ExecutionReport> reports;
    reports.reserve(parseResults.size() * 2);

    for (const ParseResult &result : parseResults) {
        if (!result.ok) {
            reports.push_back(
                ExecutionHandler::buildRejectedReport(result.order.clientOrderId, result.reason));
            continue;
        }

        auto routedReports = router.route(result.order);
        reports.insert(reports.end(), routedReports.begin(), routedReports.end());
    }

    const auto end = std::chrono::steady_clock::now();

    ordersProcessed_ = parseResults.size();
    lastRunDurationMs_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    executionReports_ = std::move(reports);

    const bool fileOpenError =
        parseResults.size() == 1 && !parseResults.front().ok &&
        parseResults.front().reason == "Failed to open input file";

    setLedState(fileOpenError ? LedState::Error : LedState::Idle);
    updateExecutionReportsTable();
    updateOrderBookTab();
    refreshUiState();

    if (fileOpenError) {
        QMessageBox::critical(this, "Run Engine",
                              "Failed to open the selected input file. Check the path and try again.");
    }

    statusBar()->showMessage(
        QString("Run complete: %1 orders processed, %2 reports generated.")
            .arg(ordersProcessed_)
            .arg(executionReports_.size()),
        5000);
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
    statusBar()->showMessage(QString("Exported reports to: %1").arg(filePath), 5000);
}

Instrument MainWindow::selectedInstrument() const {
    if (instrumentList_ == nullptr) {
        return Instrument::Rose;
    }

    const int row = instrumentList_->currentRow();
    if (row < 0 || row >= kInstrumentCount) {
        return Instrument::Rose;
    }

    return static_cast<Instrument>(row);
}

QString MainWindow::instrumentName(Instrument instrument) {
    switch (instrument) {
    case Instrument::Rose:
        return "Rose";
    case Instrument::Lavender:
        return "Lavender";
    case Instrument::Lotus:
        return "Lotus";
    case Instrument::Tulip:
        return "Tulip";
    case Instrument::Orchid:
        return "Orchid";
    }

    return "Unknown";
}

QString MainWindow::sideName(Side side) {
    switch (side) {
    case Side::Buy:
        return "Buy";
    case Side::Sell:
        return "Sell";
    }

    return "Unknown";
}

QString MainWindow::statusName(Status status) {
    switch (status) {
    case Status::New:
        return "New";
    case Status::Rejected:
        return "Rejected";
    case Status::Fill:
        return "Fill";
    case Status::PFill:
        return "PFill";
    }

    return "Unknown";
}

QString MainWindow::fixedCharToQString(const char *text, std::size_t maxLen) {
    std::size_t len = 0;
    while (len < maxLen && text[len] != '\0') {
        ++len;
    }
    return QString::fromLatin1(text, static_cast<int>(len));
}
