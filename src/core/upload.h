#pragma once
#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QVariantMap>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

class Upload final : public QObject {
  Q_OBJECT
  public:
  explicit Upload(QObject *parent = nullptr);

  static QSharedPointer<Upload> create_from_db(
      const QByteArray &id,
      const QByteArray &account_owner_id,
      const QString &path,
      int type,
      int variant,
      const QDateTime &creation);
  static QSharedPointer<Upload> get_by_uid(const QByteArray &uid);

  static QSharedPointer<Upload> create();

  void setUID(const QByteArray &uid);
  QByteArray uid() const;
  QByteArray uid_str() const { return m_uid_str; }

  QByteArray owner_uid() const;
  void setOwnerUID(const QByteArray &owner_uid);

  QString path() const;
  void setPath(const QString &path);

  int type() const;
  void setType(int type);

  int variant() const;
  void setVariant(int variant);

  ~Upload() override;

  QVariantMap to_variantmap() const;
  rapidjson::Value to_rapidjson(rapidjson::Document::AllocatorType &allocator) const;

private:
  mutable QReadWriteLock mtx_lock;

  QByteArray m_uid;
  QByteArray m_uid_str;
  QByteArray m_owner_uid;
  QString m_path;
  int m_type;
  int m_variant;
  QDateTime creation_date;
};
