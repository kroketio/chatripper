#include "upload.h"
#include "ctx.h"
#include "lib/globals.h"
#include <QUuid>
#include <QDebug>

Upload::Upload(QObject *parent) : m_type(0), m_variant(0), QObject(parent) {
  qDebug() << "new upload";
}

QSharedPointer<Upload> Upload::create() {
  auto upload = QSharedPointer<Upload>(new Upload());
  if (g::mainThread != QThread::currentThread())
    upload->moveToThread(g::mainThread);
  return upload;
}

QSharedPointer<Upload> Upload::get_by_uid(const QUuid &uid) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->uploads_lookup_uuid.value(uid);
}

QSharedPointer<Upload> Upload::create_from_db(
    const QUuid &id,
    const QUuid &account_owner_id,
    const QString &path,
    int type,
    int variant,
    const QDateTime &creation) {
  if (const auto ptr = get_by_uid(id); !ptr.isNull())
    return ptr;

  auto upload = QSharedPointer<Upload>(new Upload());
  if (g::mainThread != QThread::currentThread())
    upload->moveToThread(g::mainThread);

  upload->setUID(id);
  upload->setOwnerUID(account_owner_id);
  upload->setPath(path);
  upload->setType(type);
  upload->setVariant(variant);
  upload->creation_date = creation;

  g::ctx->upload_insert_cache(upload);
  return upload;
}

void Upload::setUID(const QUuid &uid) {
  QWriteLocker locker(&mtx_lock);
  m_uid = uid;
  m_uid_str = uid.toString(QUuid::WithoutBraces).toUtf8();
}

QUuid Upload::uid() const {
  QReadLocker locker(&mtx_lock);
  return m_uid;
}

QUuid Upload::owner_uid() const {
  QReadLocker locker(&mtx_lock);
  return m_owner_uid;
}

void Upload::setOwnerUID(const QUuid &owner_uid) {
  QWriteLocker locker(&mtx_lock);
  m_owner_uid = owner_uid;
}

QString Upload::path() const {
  QReadLocker locker(&mtx_lock);
  return m_path;
}

void Upload::setPath(const QString &path) {
  QWriteLocker locker(&mtx_lock);
  m_path = path;
}

int Upload::type() const {
  QReadLocker locker(&mtx_lock);
  return m_type;
}

void Upload::setType(int type) {
  QWriteLocker locker(&mtx_lock);
  m_type = type;
}

int Upload::variant() const {
  QReadLocker locker(&mtx_lock);
  return m_variant;
}

void Upload::setVariant(int variant) {
  QWriteLocker locker(&mtx_lock);
  m_variant = variant;
}

QVariantMap Upload::to_variantmap() const {
  QReadLocker locker(&mtx_lock);
  QVariantMap map;
  map["uid"] = m_uid;
  map["owner_uid"] = m_owner_uid;
  map["path"] = m_path;
  map["type"] = m_type;
  map["variant"] = m_variant;
  map["creation_date"] = creation_date.toString(Qt::ISODate);
  return map;
}

rapidjson::Value Upload::to_rapidjson(rapidjson::Document::AllocatorType &allocator) const {
  QReadLocker locker(&mtx_lock);
  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("uid", rapidjson::Value(m_uid_str.constData(), allocator), allocator);
  obj.AddMember("owner_uid", rapidjson::Value(m_owner_uid.toString(QUuid::WithoutBraces).toUtf8().constData(), allocator), allocator);
  obj.AddMember("path", rapidjson::Value(m_path.toUtf8().constData(), allocator), allocator);
  obj.AddMember("type", m_type, allocator);
  obj.AddMember("variant", m_variant, allocator);
  obj.AddMember("creation_date", rapidjson::Value(creation_date.toString(Qt::ISODate).toUtf8().constData(), allocator),
                allocator);
  return obj;
}

Upload::~Upload() {
  qDebug() << "RIP upload";
}
