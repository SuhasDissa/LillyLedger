#include "order_entry_widget.h"
#include "ui_order_entry_widget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
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

    setStyleSheet("BuySellToggle {"
                  "  background-color: #f5ede7;"
                  "  border: 1.5px solid #ddd0c6;"
                  "  border-radius: 8px;"
                  "}");

    connect(buyButton_, &QPushButton::clicked, this, [this]() { setSide(Side::Buy); });
    connect(sellButton_, &QPushButton::clicked, this, [this]() { setSide(Side::Sell); });

    updateStyles();
}

Side BuySellToggle::side() const {
    return currentSide_;
}

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
        buyButton_->setStyleSheet("QPushButton {"
                                  "  background-color: #d4eddf;"
                                  "  color: #1e3a2a;"
                                  "  border: none;"
                                  "  border-radius: 6px;"
                                  "  font-weight: 700;"
                                  "  font-size: 13px;"
                                  "  letter-spacing: 0.5px;"
                                  "}");
        sellButton_->setStyleSheet("QPushButton {"
                                   "  background-color: transparent;"
                                   "  color: #9a8a7e;"
                                   "  border: none;"
                                   "  border-radius: 6px;"
                                   "  font-weight: 500;"
                                   "  font-size: 13px;"
                                   "}");
        return;
    }

    buyButton_->setStyleSheet("QPushButton {"
                              "  background-color: transparent;"
                              "  color: #9a8a7e;"
                              "  border: none;"
                              "  border-radius: 6px;"
                              "  font-weight: 500;"
                              "  font-size: 13px;"
                              "}");
    sellButton_->setStyleSheet("QPushButton {"
                               "  background-color: #f9dede;"
                               "  color: #7a1f1f;"
                               "  border: none;"
                               "  border-radius: 6px;"
                               "  font-weight: 700;"
                               "  font-size: 13px;"
                               "  letter-spacing: 0.5px;"
                               "}");
}

ManualOrderEntryWidget::ManualOrderEntryWidget(QWidget *parent)
    : QWidget(parent), ui_(new Ui::ManualOrderEntryWidget) {
    ui_->setupUi(this);

    ui_->clientIdEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9]{1,7}"), ui_->clientIdEdit));

    ui_->instrumentCombo->addItem("🌹 Rose", static_cast<int>(Instrument::Rose));
    ui_->instrumentCombo->addItem("💜 Lavender", static_cast<int>(Instrument::Lavender));
    ui_->instrumentCombo->addItem("🪷 Lotus", static_cast<int>(Instrument::Lotus));
    ui_->instrumentCombo->addItem("🌷 Tulip", static_cast<int>(Instrument::Tulip));
    ui_->instrumentCombo->addItem("🌸 Orchid", static_cast<int>(Instrument::Orchid));

    ui_->submitButton->setStyleSheet("QPushButton {"
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
    ui_->resetButton->setStyleSheet("QPushButton {"
                                    "  background-color: transparent;"
                                    "  color: #9a8a7e;"
                                    "  border: 1.5px solid #ddd0c6;"
                                    "  border-radius: 8px;"
                                    "  font-size: 12px;"
                                    "}"
                                    "QPushButton:hover { color: #86522b; border-color: #c2855a; }");
    ui_->quantityErrorLabel->setStyleSheet("QLabel { color: #b84a4a; font-size: 11px; }");

    successOpacityEffect_ = new QGraphicsOpacityEffect(ui_->successLabel);
    ui_->successLabel->setGraphicsEffect(successOpacityEffect_);
    successOpacityEffect_->setOpacity(1.0);

    successHoldTimer_ = new QTimer(this);
    successHoldTimer_->setSingleShot(true);

    setupConnections();

    setStyleSheet("QWidget { background-color: #fff8f5; color: #1e1b19; font-size: 12px; }"
                  "QGroupBox { border: 1px solid #e8e2d9; border-radius: 12px; margin-top: 12px; "
                  "font-weight: 700; }"
                  "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }"
                  "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
                  " background-color: #ffffff; color: #1e1b19; border: 1px solid #d6c3b7; "
                  "border-radius: 8px; padding: 4px 8px; min-height: 34px; }"
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
                  "QLineEdit[error=\"true\"], QComboBox[error=\"true\"], QSpinBox[error=\"true\"], "
                  "QDoubleSpinBox[error=\"true\"], BuySellToggle[error=\"true\"] {"
                  " border: 1px solid #b84a4a; }");
}

ManualOrderEntryWidget::~ManualOrderEntryWidget() {
    delete ui_;
}

void ManualOrderEntryWidget::prefillPrice(double price, Side side) {
    ui_->priceSpin->setValue(price);
    ui_->sideToggle->setSide(side);

    setFieldError(ui_->priceSpin, QString());
    setFieldError(ui_->sideToggle, QString());
}

void ManualOrderEntryWidget::onSubmitClicked() {
    if (!validateForm()) {
        runSubmitShakeAnimation();
        return;
    }

    Order order{};

    const QByteArray clientIdBytes = ui_->clientIdEdit->text().trimmed().toLatin1();
    std::snprintf(order.clientOrderId, sizeof(order.clientOrderId), "%s",
                  clientIdBytes.constData());

    order.instrument = instrumentFromComboIndex(ui_->instrumentCombo->currentIndex());
    order.side = ui_->sideToggle->side();
    order.quantity = static_cast<uint16_t>(ui_->quantitySpin->value());
    order.price = ui_->priceSpin->value();

    emit orderSubmitted(order);
    resetForm();
    showSuccessMessage();
}

void ManualOrderEntryWidget::onResetClicked() {
    resetForm();
}

void ManualOrderEntryWidget::setupConnections() {
    connect(ui_->submitButton, &QPushButton::clicked, this,
            &ManualOrderEntryWidget::onSubmitClicked);
    connect(ui_->resetButton, &QPushButton::clicked, this, &ManualOrderEntryWidget::onResetClicked);

    connect(ui_->clientIdEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { setFieldError(ui_->clientIdEdit, QString()); });
    connect(ui_->instrumentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { setFieldError(ui_->instrumentCombo, QString()); });
    connect(ui_->sideToggle, &BuySellToggle::sideChanged, this,
            [this](Side) { setFieldError(ui_->sideToggle, QString()); });
    connect(ui_->quantitySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        setFieldError(ui_->quantitySpin, QString());
        ui_->quantityErrorLabel->setVisible(false);
    });
    connect(ui_->priceSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { setFieldError(ui_->priceSpin, QString()); });

    connect(successHoldTimer_, &QTimer::timeout, this, [this]() {
        auto *fade = new QPropertyAnimation(successOpacityEffect_, "opacity", this);
        fade->setDuration(400);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        connect(fade, &QPropertyAnimation::finished, this, [this]() {
            ui_->successLabel->setVisible(false);
            successOpacityEffect_->setOpacity(1.0);
        });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void ManualOrderEntryWidget::resetForm() {
    clearValidationErrors();
    ui_->clientIdEdit->clear();
    ui_->instrumentCombo->setCurrentIndex(0);
    ui_->sideToggle->setSide(Side::Buy);
    ui_->quantitySpin->setValue(10);
    ui_->priceSpin->setValue(0.01);
}

bool ManualOrderEntryWidget::validateForm() {
    clearValidationErrors();

    bool valid = true;
    const QString clientId = ui_->clientIdEdit->text().trimmed();
    const QRegularExpression regex("^[A-Za-z0-9]{1,7}$");

    if (!regex.match(clientId).hasMatch()) {
        setFieldError(ui_->clientIdEdit, "Client ID must be 1-7 alphanumeric characters");
        valid = false;
    }

    if (ui_->instrumentCombo->currentIndex() < 0) {
        setFieldError(ui_->instrumentCombo, "Select a valid instrument");
        valid = false;
    }

    if (ui_->quantitySpin->value() % 10 != 0) {
        setFieldError(ui_->quantitySpin, "Quantity must be a multiple of 10");
        ui_->quantityErrorLabel->setText("Quantity must be a multiple of 10");
        ui_->quantityErrorLabel->setVisible(true);
        valid = false;
    }

    if (ui_->priceSpin->value() <= 0.0) {
        setFieldError(ui_->priceSpin, "Price must be positive");
        valid = false;
    }

    if (ui_->sideToggle->side() != Side::Buy && ui_->sideToggle->side() != Side::Sell) {
        setFieldError(ui_->sideToggle, "Select Buy or Sell");
        valid = false;
    }

    return valid;
}

void ManualOrderEntryWidget::clearValidationErrors() {
    setFieldError(ui_->clientIdEdit, QString());
    setFieldError(ui_->instrumentCombo, QString());
    setFieldError(ui_->sideToggle, QString());
    setFieldError(ui_->quantitySpin, QString());
    setFieldError(ui_->priceSpin, QString());
    ui_->quantityErrorLabel->setVisible(false);
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
    const QPoint basePos = ui_->submitButton->pos();

    auto *anim = new QPropertyAnimation(ui_->submitButton, "pos", this);
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
    ui_->successLabel->setVisible(true);
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
