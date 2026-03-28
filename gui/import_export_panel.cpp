#include "import_export_panel.h"
#include "ui_import_export_panel.h"

#include "engine/executionhandler.h"
#include "engine/orderrouter.h"
#include "io/csvreader.h"
#include "io/csvwriter.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLocale>
#include <QMetaObject>
#include <QScrollBar>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <QtGlobal>
#include <vector>

namespace {
constexpr int kControlHeight = 34;
constexpr int kPreviewRowHeight = 34;
} // namespace

EngineWorker::EngineWorker(QString inputFilePath, QString outputFilePath, QObject *parent)
    : QObject(parent), inputFilePath_(std::move(inputFilePath)),
      outputFilePath_(std::move(outputFilePath)) {}

void EngineWorker::run() {
    emit progress(0);

    QElapsedTimer totalTimer;
    totalTimer.start();

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    CSVReader reader(inputFilePath_.toStdString());
    const std::vector<ParseResult> parseResults = reader.readAll();
    const double parseMs = static_cast<double>(phaseTimer.nsecsElapsed()) / 1000000.0;

    if (isCancelled()) {
        emit error(timestamped("Run cancelled"));
        return;
    }

    emit progress(33);
    emit logLine(
        timestamped(QString("Parsed %1 orders")
                        .arg(QLocale().toString(static_cast<qlonglong>(parseResults.size())))));

    phaseTimer.restart();

    OrderRouter router;
    std::vector<ExecutionReport> allReports;
    allReports.reserve(parseResults.size() * 2);

    for (const ParseResult &result : parseResults) {
        if (isCancelled()) {
            emit error(timestamped("Run cancelled"));
            return;
        }

        if (!result.ok) {
            allReports.push_back(
                ExecutionHandler::buildRejectedReport(result.order.clientOrderId, result.reason));
            continue;
        }

        auto reports = router.route(result.order);
        allReports.insert(allReports.end(), reports.begin(), reports.end());
    }

    const double matchMs = static_cast<double>(phaseTimer.nsecsElapsed()) / 1000000.0;

    emit progress(66);
    emit logLine(
        timestamped(QString("Matching complete — %1 reports generated")
                        .arg(QLocale().toString(static_cast<qlonglong>(allReports.size())))));

    if (isCancelled()) {
        emit error(timestamped("Run cancelled"));
        return;
    }

    phaseTimer.restart();
    CSVWriter writer(outputFilePath_.toStdString());
    writer.write(allReports);
    const double writeMs = static_cast<double>(phaseTimer.nsecsElapsed()) / 1000000.0;

    emit progress(100);
    emit logLine(timestamped(QString("Written to %1").arg(outputFilePath_)));

    const double totalMs = static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0;
    emit logLine(timestamped(QString("Parse: %1ms | Match: %2ms | Write: %3ms | Total: %4ms")
                                 .arg(parseMs, 0, 'f', 1)
                                 .arg(matchMs, 0, 'f', 1)
                                 .arg(writeMs, 0, 'f', 1)
                                 .arg(totalMs, 0, 'f', 1)));

    QVector<ExecutionReport> reports;
    reports.reserve(static_cast<qsizetype>(allReports.size()));
    for (const ExecutionReport &report : allReports) {
        reports.push_back(report);
    }

    emit finished(reports);
}

void EngineWorker::requestCancel() {
    cancelRequested_.store(true, std::memory_order_relaxed);
}

bool EngineWorker::isCancelled() const {
    return cancelRequested_.load(std::memory_order_relaxed);
}

QString EngineWorker::timestamped(const QString &message) {
    return QString("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss")).arg(message);
}

ImportExportPanel::ImportExportPanel(QWidget *parent)
    : QWidget(parent), ui_(new Ui::ImportExportPanel) {
    qRegisterMetaType<ExecutionReport>("ExecutionReport");
    qRegisterMetaType<QVector<ExecutionReport>>("QVector<ExecutionReport>");

    ui_->setupUi(this);

    // Preview table header configuration
    ui_->previewTable->setHorizontalHeaderLabels(
        {"Client Order ID", "Instrument", "Side", "Qty", "Price"});
    ui_->previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->previewTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui_->previewTable->setWordWrap(false);
    ui_->previewTable->verticalHeader()->setVisible(false);
    ui_->previewTable->verticalHeader()->setDefaultSectionSize(kPreviewRowHeight);
    auto *previewHeader = ui_->previewTable->horizontalHeader();
    previewHeader->setMinimumSectionSize(56);
    previewHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    previewHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    previewHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    previewHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    previewHeader->setSectionResizeMode(4, QHeaderView::Stretch);

    // Log view font
    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui_->logView->setFont(monoFont);
    ui_->logView->setStyleSheet(
        "QPlainTextEdit { background-color: #fdf8f4; color: #52443c; border: 1px solid #e8e2d9; }");

    ui_->runButton->setStyleSheet("QPushButton {"
                                  " background-color: #86522b;"
                                  " color: #ffffff;"
                                  " border: 1px solid #754624;"
                                  " border-radius: 8px;"
                                  " font-weight: 600;"
                                  "}"
                                  "QPushButton:hover {"
                                  " background-color: #754624;"
                                  "}"
                                  "QPushButton:disabled {"
                                  " background-color: #d6c3b7;"
                                  " color: #8a776b;"
                                  "}");

    setStyleSheet("QWidget { background-color: #fff8f5; color: #1e1b19; font-size: 12px; }"
                  "QGroupBox {"
                  " border: 1px solid #e8e2d9;"
                  " border-radius: 10px;"
                  " margin-top: 10px;"
                  " font-weight: 700;"
                  "}"
                  "QGroupBox::title {"
                  " subcontrol-origin: margin;"
                  " left: 10px;"
                  " padding: 0 6px;"
                  " color: #52443c;"
                  "}"
                  "QLineEdit, QTableWidget, QProgressBar, QPlainTextEdit {"
                  " background-color: #ffffff;"
                  " border: 1px solid #e8e2d9;"
                  " border-radius: 6px;"
                  " color: #1e1b19;"
                  "}"
                  "QLineEdit { min-height: 34px; padding: 4px 8px; }"
                  "QHeaderView::section {"
                  " background-color: #f5f0ea;"
                  " color: #84746a;"
                  " border: 1px solid #e8e2d9;"
                  " padding: 8px 10px;"
                  "}"
                  "QPushButton {"
                  " background-color: #ffffff;"
                  " border: 1px solid #d6c3b7;"
                  " border-radius: 8px;"
                  " color: #52443c;"
                  " padding: 5px 10px;"
                  " min-height: 34px;"
                  "}"
                  "QPushButton:hover { background-color: #fdf8f4; }");

    setupConnections();
    setRunningState(false);
}

ImportExportPanel::~ImportExportPanel() {
    if (worker_ != nullptr) {
        QMetaObject::invokeMethod(worker_, &EngineWorker::requestCancel, Qt::QueuedConnection);
    }

    if (workerThread_ != nullptr) {
        workerThread_->quit();
        workerThread_->wait();
    }
}

void ImportExportPanel::onBrowseInputClicked() {
    const QString filePath =
        QFileDialog::getOpenFileName(this, "Select Input CSV", {}, "CSV Files (*.csv)");
    if (filePath.isEmpty()) {
        return;
    }

    ui_->inputPathEdit->setText(filePath);
    loadInputPreview(filePath);

    if (ui_->outputPathEdit->text().isEmpty() ||
        ui_->outputPathEdit->text() == autoGeneratedOutputPath_) {
        autoGeneratedOutputPath_ = autoOutputPathForInput(filePath);
        ui_->outputPathEdit->setText(autoGeneratedOutputPath_);
    }

    if (!runInProgress_) {
        ui_->runButton->setEnabled(true);
    }

    appendLogLine(timestamped(QString("Input selected: %1").arg(filePath)));
}

void ImportExportPanel::onBrowseOutputClicked() {
    const QString initialPath = ui_->outputPathEdit->text();
    const QString filePath =
        QFileDialog::getSaveFileName(this, "Select Output CSV", initialPath, "CSV Files (*.csv)");
    if (filePath.isEmpty()) {
        return;
    }

    ui_->outputPathEdit->setText(filePath);
    autoGeneratedOutputPath_.clear();
    appendLogLine(timestamped(QString("Output selected: %1").arg(filePath)));
}

void ImportExportPanel::onRunButtonClicked() {
    if (runInProgress_) {
        if (worker_ != nullptr) {
            QMetaObject::invokeMethod(worker_, &EngineWorker::requestCancel, Qt::QueuedConnection);
            appendLogLine(timestamped("Cancellation requested"));
        }
        return;
    }

    startWorker();
}

void ImportExportPanel::onClearLogClicked() {
    ui_->logView->clear();
}

void ImportExportPanel::onWorkerProgress(int percent) {
    ui_->progressBar->setValue(percent);
}

void ImportExportPanel::onWorkerLogLine(const QString &line) {
    appendLogLine(line);
}

void ImportExportPanel::onWorkerFinished(QVector<ExecutionReport> reports) {
    emit resultsReady(reports);
}

void ImportExportPanel::onWorkerError(QString message) {
    appendLogLine(message);
}

void ImportExportPanel::onWorkerThreadFinished() {
    if (workerThread_ != nullptr) {
        workerThread_->deleteLater();
        workerThread_ = nullptr;
    }

    worker_ = nullptr;
    setRunningState(false);
}

void ImportExportPanel::setupConnections() {
    connect(ui_->inputBrowseButton, &QPushButton::clicked, this,
            &ImportExportPanel::onBrowseInputClicked);
    connect(ui_->outputBrowseButton, &QPushButton::clicked, this,
            &ImportExportPanel::onBrowseOutputClicked);
    connect(ui_->runButton, &QPushButton::clicked, this, &ImportExportPanel::onRunButtonClicked);
    connect(ui_->clearLogButton, &QPushButton::clicked, this,
            &ImportExportPanel::onClearLogClicked);
}

void ImportExportPanel::loadInputPreview(const QString &filePath) {
    ui_->previewTable->clearContents();
    ui_->previewTable->setRowCount(0);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui_->rowCountLabel->setText("0 rows detected");
        appendLogLine(
            timestamped(QString("Failed to open input file for preview: %1").arg(filePath)));
        return;
    }

    QTextStream stream(&file);
    qsizetype totalRows = 0;
    bool firstNonEmptySeen = false;
    QVector<QStringList> previewRows;
    previewRows.reserve(10);

    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        if (rawLine.trimmed().isEmpty()) {
            continue;
        }

        const QStringList tokens = splitCommaSeparated(rawLine);
        if (!firstNonEmptySeen) {
            firstNonEmptySeen = true;
            if (isHeaderRow(tokens)) {
                continue;
            }
        }

        ++totalRows;
        if (previewRows.size() < 10) {
            QStringList row = tokens;
            while (row.size() < 5) {
                row.push_back("");
            }
            if (row.size() > 5) {
                row = row.mid(0, 5);
            }
            previewRows.push_back(row);
        }
    }

    ui_->previewTable->setRowCount(previewRows.size());
    for (int row = 0; row < previewRows.size(); ++row) {
        const QStringList &fields = previewRows.at(row);
        for (int col = 0; col < 5; ++col) {
            auto *item = new QTableWidgetItem(fields.at(col));
            ui_->previewTable->setItem(row, col, item);
        }
    }

    ui_->rowCountLabel->setText(
        QString("%1 rows detected").arg(QLocale().toString(static_cast<qlonglong>(totalRows))));
}

void ImportExportPanel::appendLogLine(const QString &line) {
    ui_->logView->appendPlainText(line);
    auto *scrollBar = ui_->logView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ImportExportPanel::setRunningState(bool running) {
    runInProgress_ = running;

    if (running) {
        ui_->progressBar->setVisible(true);
        ui_->progressBar->setValue(0);
        ui_->runButton->setText("⏹ Cancel");
        ui_->runButton->setEnabled(true);
        ui_->inputBrowseButton->setEnabled(false);
        ui_->outputBrowseButton->setEnabled(false);
        return;
    }

    ui_->progressBar->setVisible(false);
    ui_->runButton->setText("▶ Run Matching Engine");
    ui_->runButton->setEnabled(!ui_->inputPathEdit->text().trimmed().isEmpty());
    ui_->inputBrowseButton->setEnabled(true);
    ui_->outputBrowseButton->setEnabled(true);
}

void ImportExportPanel::startWorker() {
    const QString inputPath = ui_->inputPathEdit->text().trimmed();
    if (inputPath.isEmpty()) {
        return;
    }

    const QString outputPath = ensureOutputPath();

    if (workerThread_ != nullptr) {
        return;
    }

    workerThread_ = new QThread(this);
    worker_ = new EngineWorker(inputPath, outputPath);
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::started, worker_, &EngineWorker::run);
    connect(worker_, &EngineWorker::progress, this, &ImportExportPanel::onWorkerProgress);
    connect(worker_, &EngineWorker::logLine, this, &ImportExportPanel::onWorkerLogLine);
    connect(worker_, &EngineWorker::finished, this, &ImportExportPanel::onWorkerFinished);
    connect(worker_, &EngineWorker::error, this, &ImportExportPanel::onWorkerError);

    connect(worker_, &EngineWorker::finished, workerThread_, &QThread::quit);
    connect(worker_, &EngineWorker::error, workerThread_, &QThread::quit);
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, this, &ImportExportPanel::onWorkerThreadFinished);

    appendLogLine(timestamped("Engine run started"));
    setRunningState(true);
    workerThread_->start();
}

QString ImportExportPanel::ensureOutputPath() {
    const QString existing = ui_->outputPathEdit->text().trimmed();
    if (!existing.isEmpty()) {
        return existing;
    }

    const QString generated = autoOutputPathForInput(ui_->inputPathEdit->text().trimmed());
    autoGeneratedOutputPath_ = generated;
    ui_->outputPathEdit->setText(generated);
    return generated;
}

QString ImportExportPanel::autoOutputPathForInput(const QString &inputFilePath) {
    const QFileInfo inputInfo(inputFilePath);
    const QString baseName =
        inputInfo.completeBaseName().isEmpty() ? "orders" : inputInfo.completeBaseName();
    return QDir(inputInfo.absolutePath()).filePath(baseName + "_out.csv");
}

QString ImportExportPanel::timestamped(const QString &message) {
    return QString("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss")).arg(message);
}

QStringList ImportExportPanel::splitCommaSeparated(const QString &line) {
    QStringList tokens = line.split(',', Qt::KeepEmptyParts);
    for (QString &token : tokens) {
        token = token.trimmed();
    }
    return tokens;
}

bool ImportExportPanel::isHeaderRow(const QStringList &tokens) {
    if (tokens.size() != 5) {
        return false;
    }

    return tokens[0] == "Client Order ID" && tokens[1] == "Instrument" && tokens[2] == "Side" &&
           tokens[3] == "Quantity" && tokens[4] == "Price";
}
