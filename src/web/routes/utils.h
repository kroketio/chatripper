#pragma once

#include <QHttpServerRequest>
#include <QHttpHeaders>
#include <QHostAddress>
#include <QString>

#include "lib/globals.h"

// Returns the IP address of the client, preferring X-Forwarded-For if present
QHostAddress ipFromRequest(const QHttpServerRequest &req);

// Extracts the session token from the request's cookies
QString tokenFromRequest(const QHttpServerRequest &req);

// Extract session token from cookie headers
QString tokenFromCookies(const QStringList &cookies);

bool is_authenticated(const QHttpServerRequest &request);