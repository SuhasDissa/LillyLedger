#pragma once

#include "core/executionreport.h"
#include "core/order.h"
#include <QMetaType>
#include <QVector>

Q_DECLARE_METATYPE(ExecutionReport)
Q_DECLARE_METATYPE(QVector<ExecutionReport>)
Q_DECLARE_METATYPE(Order)
