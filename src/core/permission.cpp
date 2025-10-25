#include "permission.h"
#include "ctx.h"
#include "lib/globals.h"
#include <QUuid>
#include <QDebug>

Permission::Permission(QObject *parent) :
  m_flags(0), QObject(parent) {
  qDebug() << "new permission";
}

QSharedPointer<Permission> Permission::create() {
  auto perm = QSharedPointer<Permission>(new Permission());
  if (g::mainThread != QThread::currentThread())
    perm->moveToThread(g::mainThread);
  return perm;
}

QSharedPointer<Permission> Permission::create_from_db(
    const QUuid &id,
    const QUuid &role_id,
    int permission_bits,
    const QDateTime &creation) {
  if (const auto ptr = get_by_uid(id); !ptr.isNull())
    return ptr;

  auto perm = QSharedPointer<Permission>(new Permission());
  if (g::mainThread != QThread::currentThread())
    perm->moveToThread(g::mainThread);

  perm->setUID(id);
  perm->setRoleUID(role_id);
  perm->setFlags(PermissionFlags(permission_bits));
  perm->creation_date = creation;

  g::ctx->permission_insert_cache(perm);
  return perm;
}

void Permission::setUID(const QUuid &uid) {
  QWriteLocker locker(&mtx_lock);
  m_uid = uid;
  m_uid_str = uid.toString(QUuid::WithoutBraces).toUtf8();
}

QUuid Permission::uid() const {
  QReadLocker locker(&mtx_lock);
  return m_uid;
}

QUuid Permission::role_uid() const {
  QReadLocker locker(&mtx_lock);
  return m_role_uid;
}

void Permission::setRoleUID(const QUuid &role_uid) {
  QWriteLocker locker(&mtx_lock);
  m_role_uid = role_uid;
}

Permission::PermissionFlags Permission::flags() const {
  QReadLocker locker(&mtx_lock);
  return m_flags;
}

void Permission::setFlags(PermissionFlags flags) {
  QWriteLocker locker(&mtx_lock);
  m_flags = flags;
}

QSharedPointer<Permission> Permission::get_by_uid(const QUuid &uid) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->permissions_lookup_uuid.value(uid);
}

QVariantMap Permission::to_variantmap() const {
  QReadLocker locker(&mtx_lock);
  QVariantMap map;
  map["uid"] = m_uid;
  map["role_uid"] = m_role_uid;
  map["flags"] = m_flags.bits;
  map["creation_date"] = creation_date.toString(Qt::ISODate);
  return map;
}

rapidjson::Value Permission::to_rapidjson(rapidjson::Document::AllocatorType &allocator) const {
  QReadLocker locker(&mtx_lock);
  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("uid", rapidjson::Value(m_uid_str.constData(), allocator), allocator);
  obj.AddMember("role_uid", rapidjson::Value(m_role_uid.toString(QUuid::WithoutBraces).toUtf8().constData(), allocator), allocator);
  obj.AddMember("flags", m_flags.bits, allocator);
  obj.AddMember("creation_date", rapidjson::Value(creation_date.toString(Qt::ISODate).toUtf8().constData(), allocator), allocator);
  return obj;
}
