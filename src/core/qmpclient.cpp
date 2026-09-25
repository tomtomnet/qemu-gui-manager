// SPDX-License-Identifier: GPL-2.0-or-later
#include "qmpclient.h"

/* STUB: to be implemented */

struct QmpClient::Private
{
};

QmpClient::QmpClient(QObject *parent) : QObject(parent), d(new Private)
{
}

QmpClient::~QmpClient()
{
    delete d;
}

void QmpClient::connectToSocket(const QString &)
{
}

void QmpClient::disconnectFromSocket()
{
}

bool QmpClient::isReady() const
{
    return false;
}

void QmpClient::execute(const QString &, const QJsonObject &, Callback callback)
{
    if (callback) {
        callback({}, QStringLiteral("not implemented"));
    }
}
