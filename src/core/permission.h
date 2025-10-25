#pragma once
#include <QObject>
#include <QByteArray>
#include <QUuid>
#include <QDateTime>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QVariantMap>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include "lib/bitflags.h"

class Permission final : public QObject {
  Q_OBJECT
  public:
  enum class Flags : int {
    CanView   = 1 << 0,
    CanSend   = 1 << 1,
    CanManage = 1 << 2,
    CanAssign = 1 << 3
};

  using PermissionFlags = ::Flags<Flags>;

  explicit Permission(QObject *parent = nullptr);

  static QSharedPointer<Permission> create_from_db(
      const QUuid &id,
      const QUuid &role_id,
      int permission_bits,
      const QDateTime &creation = QDateTime::currentDateTime());

  static QSharedPointer<Permission> create();

  static QSharedPointer<Permission> get_by_uid(const QUuid &uid);

  void setUID(const QUuid &uid);
  QUuid uid() const;
  QByteArray uid_str() const { return m_uid_str; }

  QUuid role_uid() const;
  void setRoleUID(const QUuid &role_uid);

  PermissionFlags flags() const;
  void setFlags(PermissionFlags flags);

  QDateTime creation_date;

  QVariantMap to_variantmap() const;
  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType &allocator) const;

private:
  mutable QReadWriteLock mtx_lock;

  QUuid m_uid;
  QByteArray m_uid_str;
  QUuid m_role_uid;
  PermissionFlags m_flags;
};
