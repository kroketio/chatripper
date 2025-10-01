#pragma once

#include <QHttpServerRequest>
#include <QHttpHeaders>
#include <QHostAddress>
#include <QString>
#include <QMimeDatabase>
#include <QMimeType>
#include <QFileInfo>
#include <QHttpServerResponse>

// Returns the IP address of the client, preferring X-Forwarded-For if present
QHostAddress ipFromRequest(const QHttpServerRequest &req);

// Extracts the session token from the request's cookies
QString tokenFromRequest(const QHttpServerRequest &req);

// Extract session token from cookie headers
QString tokenFromCookies(const QStringList &cookies);

// Mimetype info
QString detectMimeType(const QString &filePath);

// Path trickery
inline bool sanitizePath(const QString &path, QString &outSafePath) {
  const QString safePath = QFileInfo(path).filePath();

  if (safePath.contains("..")) {
    return false;
  }

  outSafePath = safePath;
  return true;
}