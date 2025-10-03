#pragma once
#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QSharedPointer>
#include <QReadWriteLock>
#include <QVariantMap>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

class Server;

class Role final : public QObject {
  Q_OBJECT
  public:
  explicit Role(const QByteArray &role_name = "", QObject *parent = nullptr);

  static QSharedPointer<Role> create_from_db(
      const QByteArray &id,
      const QByteArray &server_id,
      const QByteArray &name,
      const QByteArray &icon,
      int color,
      int priority,
      const QDateTime &creation);

  static QSharedPointer<Role> create();

  static QSharedPointer<Role> get_by_uid(const QByteArray &uid);
  static QSharedPointer<Role> get_by_name(const QByteArray &name);

  void setUID(const QByteArray &uid);
  QByteArray uid() const;
  QByteArray uid_str() const { return m_uid_str; }

  QByteArray name() const;
  void setName(const QByteArray &name);

  QByteArray server_uid() const;
  void setServerUID(const QByteArray &server_uid);

  QByteArray icon_uid() const;
  void setIconUID(const QByteArray &icon_uid);

  int color() const;
  void setColor(int color);

  int priority() const;
  void setPriority(int priority);

  ~Role() override;

  QVariantMap to_variantmap() const;
  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType &allocator) const;

private:
  mutable QReadWriteLock mtx_lock;

  QByteArray m_uid;
  QByteArray m_uid_str;
  QByteArray m_name;
  QByteArray m_server_uid;
  QByteArray m_icon_uid;
  int m_color;
  int m_priority;
  QDateTime creation_date;
};
