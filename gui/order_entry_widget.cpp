#include "order_entry_widget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

BuySellToggle::BuySellToggle(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(3);

    buyButton_ = new QPushButton("BUY", this);
    sellButton_ = new QPushButton("SELL", this);

    buyButton_->setCheckable(true);
    sellButton_->setCheckable(true);
    buyButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sellButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buyButton_->setMinimumHeight(34);
    sellButton_->setMinimumHeight(34);

    layout->addWidget(buyButton_);
    layout->addWidget(sellButton_);

    setStyleSheet(
        "BuySellToggle {"
        "  background-color: #f5ede7;"
        "  border: 1.5px solid #ddd0c6;"
        "  border-radius: 8px;"
        "}");

    connect(buyButton_, &QPushButton::clicked, this, [this]() { setSide(Side::Buy); });
    connect(sellButton_, &QPushButton::clicked, this, [this]() { setSide(Side::Sell); });

    updateStyles();
}

Side BuySellToggle::side() const { return currentSide_; }

void BuySellToggle::setSide(Side side) {
    if (currentSide_ == side) {
        updateStyles();
        return;
    }

    currentSide_ = side;
    updateStyles();
    emit sideChanged(currentSide_);
}

void BuySellToggle::updateStyles() {
    buyButton_->setChecked(currentSide_ == Side::Buy);
    sellButton_->setChecked(currentSide_ == Side::Sell);

    if (currentSide_ == Side::Buy) {
        buyButton_->setStyleSheet(
            "QPushButton {"
            "  background-color: #d4eddf;"
            "  color: #1e3a2a;"
            "  border: none;"
            "  border-radius: 6px;"
            "  font-weight: 700;"
            "  font-size: 13px;"
            "  letter-spacing: 0.5px;"
            "}");
        sellButton_->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent;"
            "  color: #9a8a7e;"
            "  border: none;"
            "  border-radius: 6px;"
            "  font-weight: 500;"
            "  font-size: 13px;"
            "}");
        return;
    }

    buyButton_->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #9a8a7e;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-weight: 500;"
        "  font-size: 13px;"
        "}");
    sellButton_->setStyleSheet(
        "QPushButton {"
        "  background-color: #f9dede;"
        "  color: #7a1f1f;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-weight: 700;"
        "  font-size: 13px;"
        "  letter-spacing: 0.5px;"
        "}");
}

ManualOrderEntryWidget::ManualOrderEntryWidget(QWidget *parent) : QWidget(parent) {
    setupUi();
    setupConnections();
}

void ManualOrderEntryWidget::prefillPrice(double price, Side side) {
    priceSpin_->setValue(price);
    sideToggle_->setSide(side);

    setFieldError(priceSpin_, QString());
    setFieldError(sideToggle_, QString());
}

void ManualOrderEntryWidget::onSubmitClicked() {
    if (!validateForm()) {
        runSubmitShakeAnimation();
        return;
    }

    Order order{};

    const QByteArray clientIdBytes = clientIdEdit_->text().trimmed().toLatin1();
    std::snprintf(order.clientOrderId, sizeof(order.clientOrderId), "%s", clientIdBytes.constData());

    order.instrument = instrumentFromComboIndex(instrumentCombo_->currentIndex());
    order.side = sideToggle_->side();
    order.quantity = static_cast<uint16_t>(quantitySpin_->value());
    order.price = priceSpin_->value();

    emit orderSubmitted(order);
    resetForm();
    showSuccessMessage();
}

void ManualOrderEntryWidget::onResetClicked() { resetForm(); }

void ManualOrderEntryWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scroll area so form is reachable on small screens
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background-color: #faf8f4; border: none; }");

    auto *scrollContent = new QWidget(scrollArea);
    scrollContent->setStyleSheet("QWidget { background-color: #faf8f4; }");
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(48, 40, 48, 40);
    scrollLayout->setSpacing(0);
    scrollLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    orderGroup_ = new QGroupBox("NEW ORDER", scrollContent);
    orderGroup_->setFixedWidth(520);
    auto *formLayout = new QFormLayout(orderGroup_);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignTop);
    formLayout->setHorizontalSpacing(20);
    formLayout->setVerticalSpacing(16);

    clientIdEdit_ = new QLineEdit(orderGroup_);
    clientIdEdit_->setMaxLength(7);
    clientIdEdit_->setPlaceholderText("e.g. CL1234");
    clientIdEdit_->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9]{1,7}"), clientIdEdit_));
    formLayout->addRow("Client ID", clientIdEdit_);

    instrumentCombo_ = new QComboBox(orderGroup_);
    instrumentCombo_->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    instrumentCombo_->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    instrumentCombo_->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    instrumentCombo_->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    instrumentCombo_->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));
    formLayout->addRow("Instrument", instrumentCombo_);

    sideToggle_ = new BuySellToggle(orderGroup_);
    formLayout->addRow("Side", sideToggle_);

    quantityContainer_ = new QWidget(orderGroup_);
    auto *quantityLayout = new QVBoxLayout(quantityContainer_);
    quantityLayout->setContentsMargins(0, 0, 0, 0);
    quantityLayout->setSpacing(3);

    quantitySpin_ = new QSpinBox(quantityContainer_);
    quantitySpin_->setRange(10, 1000);
    quantitySpin_->setSingleStep(10);
    quantitySpin_->setSuffix(" lots");
    quantitySpin_->setValue(10);
    quantityLayout->addWidget(quantitySpin_);

    quantityErrorLabel_ = new QLabel("Quantity must be a multiple of 10", quantityContainer_);
    quantityErrorLabel_->setStyleSheet("QLabel { color: #b84a4a; font-size: 11px; }");
    quantityErrorLabel_->setVisible(false);
    quantityLayout->addWidget(quantityErrorLabel_);

    formLayout->addRow("Quantity", quantityContainer_);

    priceSpin_ = new QDoubleSpinBox(orderGroup_);
    priceSpin_->setRange(0.01, 99999.99);
    priceSpin_->setSingleStep(0.01);
    priceSpin_->setPrefix("$");
    priceSpin_->setDecimals(2);
    priceSpin_->setValue(0.01);
    formLayout->addRow("Price", priceSpin_);

    auto *submitContainer = new QWidget(orderGroup_);
    auto *submitLayout = new QVBoxLayout(submitContainer);
    submitLayout->setContentsMargins(0, 0, 0, 0);
    submitLayout->setSpacing(6);

    submitButton_ = new QPushButton("Submit Order →", submitContainer);
    submitButton_->setMinimumHeight(48);
    submitButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    submitButton_->setStyleSheet(
        "QPushButton {"
        "  background-color: #86522b;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-weight: 700;"
        "  font-size: 13px;"
        "  letter-spacing: 0.5px;"
        "}"
        "QPushButton:hover   { background-color: #74461f; }"
        "QPushButton:pressed { background-color: #613a19; }");

    resetButton_ = new QPushButton("Reset", submitContainer);
    resetButton_->setMinimumHeight(36);
    resetButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resetButton_->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #9a8a7e;"
        "  border: 1.5px solid #ddd0c6;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { color: #86522b; border-color: #c2855a; }");

    successLabel_ = new QLabel("✓ Order submitted", submitContainer);
    successLabel_->setStyleSheet("QLabel { color: #2c694d; font-weight: 600; }");
    successLabel_->setVisible(false);

    successOpacityEffect_ = new QGraphicsOpacityEffect(successLabel_);
    successLabel_->setGraphicsEffect(successOpacityEffect_);
    successOpacityEffect_->setOpacity(1.0);

    submitLayout->addWidget(submitButton_);
    submitLayout->addWidget(resetButton_);
    submitLayout->addWidget(successLabel_);
    formLayout->addRow(QString(), submitContainer);

    scrollLayout->addWidget(orderGroup_, 0, Qt::AlignTop | Qt::AlignHCenter);
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    successHoldTimer_ = new QTimer(this);
    successHoldTimer_->setSingleShot(true);

    setStyleSheet(
        "QWidget { background-color: #fff8f5; color: #1e1b19; font-size: 12px; }"
        "QGroupBox { border: 1px solid #e8e2d9; border-radius: 12px; margin-top: 12px; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        " background-color: #ffffff; color: #1e1b19; border: 1px solid #d6c3b7; border-radius: 8px; padding: 4px 8px; min-height: 34px; }"
        "QComboBox::drop-down {"
        " subcontrol-origin: padding;"
        " subcontrol-position: top right;"
        " width: 18px;"
        " border-left: 1px solid #d6c3b7;"
        "}"
        "QComboBox QAbstractItemView {"
        " background-color: #ffffff;"
        " color: #52443c;"
        " border: 1px solid #e8e2d9;"
        " selection-background-color: #f5f0ea;"
        " selection-color: #1e1b19;"
        " outline: 0px;"
        "}"
        "QLineEdit[error=\"true\"], QComboBox[error=\"true\"], QSpinBox[error=\"true\"], QDoubleSpinBox[error=\"true\"], BuySellToggle[error=\"true\"] {"
        " border: 1px solid #b84a4a; }");
}

void ManualOrderEntryWidget::setupConnections() {
    connect(submitButton_, &QPushButton::clicked, this, &ManualOrderEntryWidget::onSubmitClicked);
    connect(resetButton_, &QPushButton::clicked, this, &ManualOrderEntryWidget::onResetClicked);

    connect(clientIdEdit_, &QLineEdit::textChanged, this, [this](const QString &) {
        setFieldError(clientIdEdit_, QString());
    });
    connect(instrumentCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { setFieldError(instrumentCombo_, QString()); });
    connect(sideToggle_, &BuySellToggle::sideChanged, this,
            [this](Side) { setFieldError(sideToggle_, QString()); });
    connect(quantitySpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        setFieldError(quantitySpin_, QString());
        quantityErrorLabel_->setVisible(false);
    });
    connect(priceSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { setFieldError(priceSpin_, QString()); });

    connect(successHoldTimer_, &QTimer::timeout, this, [this]() {
        auto *fade = new QPropertyAnimation(successOpacityEffect_, "opacity", this);
        fade->setDuration(400);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        connect(fade, &QPropertyAnimation::finished, this, [this]() {
            successLabel_->setVisible(false);
            successOpacityEffect_->setOpacity(1.0);
        });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void ManualOrderEntryWidget::resetForm() {
    clearValidationErrors();
    clientIdEdit_->clear();
    instrumentCombo_->setCurrentIndex(0);
    sideToggle_->setSide(Side::Buy);
    quantitySpin_->setValue(10);
    priceSpin_->setValue(0.01);
}

bool ManualOrderEntryWidget::validateForm() {
    clearValidationErrors();

    bool valid = true;
    const QString clientId = clientIdEdit_->text().trimmed();
    const QRegularExpression regex("^[A-Za-z0-9]{1,7}$");

    if (!regex.match(clientId).hasMatch()) {
        setFieldError(clientIdEdit_, "Client ID must be 1-7 alphanumeric characters");
        valid = false;
    }

    if (instrumentCombo_->currentIndex() < 0) {
        setFieldError(instrumentCombo_, "Select a valid instrument");
        valid = false;
    }

    if (quantitySpin_->value() % 10 != 0) {
        setFieldError(quantitySpin_, "Quantity must be a multiple of 10");
        quantityErrorLabel_->setText("Quantity must be a multiple of 10");
        quantityErrorLabel_->setVisible(true);
        valid = false;
    }

    if (priceSpin_->value() <= 0.0) {
        setFieldError(priceSpin_, "Price must be positive");
        valid = false;
    }

    if (sideToggle_->side() != Side::Buy && sideToggle_->side() != Side::Sell) {
        setFieldError(sideToggle_, "Select Buy or Sell");
        valid = false;
    }

    return valid;
}

void ManualOrderEntryWidget::clearValidationErrors() {
    setFieldError(clientIdEdit_, QString());
    setFieldError(instrumentCombo_, QString());
    setFieldError(sideToggle_, QString());
    setFieldError(quantitySpin_, QString());
    setFieldError(priceSpin_, QString());
    quantityErrorLabel_->setVisible(false);
}

void ManualOrderEntryWidget::setFieldError(QWidget *field, const QString &message) {
    const bool hasError = !message.isEmpty();
    field->setProperty("error", hasError);
    field->setToolTip(message);
    field->style()->unpolish(field);
    field->style()->polish(field);
    field->update();
}

void ManualOrderEntryWidget::runSubmitShakeAnimation() {
    const QPoint basePos = submitButton_->pos();

    auto *anim = new QPropertyAnimation(submitButton_, "pos", this);
    anim->setDuration(300);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    anim->setKeyValueAt(0.0, basePos);
    anim->setKeyValueAt(0.1, basePos + QPoint(-8, 0));
    anim->setKeyValueAt(0.2, basePos + QPoint(8, 0));
    anim->setKeyValueAt(0.3, basePos + QPoint(-6, 0));
    anim->setKeyValueAt(0.4, basePos + QPoint(6, 0));
    anim->setKeyValueAt(0.5, basePos + QPoint(-4, 0));
    anim->setKeyValueAt(0.6, basePos + QPoint(4, 0));
    anim->setKeyValueAt(1.0, basePos);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ManualOrderEntryWidget::showSuccessMessage() {
    successHoldTimer_->stop();
    successLabel_->setVisible(true);
    successOpacityEffect_->setOpacity(1.0);
    successHoldTimer_->start(2000);
}

Instrument ManualOrderEntryWidget::instrumentFromComboIndex(int index) {
    switch (index) {
    case 0:
        return Instrument::Rose;
    case 1:
        return Instrument::Lavender;
    case 2:
        return Instrument::Lotus;
    case 3:
        return Instrument::Tulip;
    case 4:
        return Instrument::Orchid;
    default:
        return Instrument::Rose;
    }
}
