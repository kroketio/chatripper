#include "role.h"
#include "ctx.h"
#include "lib/globals.h"
#include <QUuid>
#include <QDebug>

Role::Role(const QByteArray &role_name, QObject *parent) :
  m_name(role_name), m_color(0), m_priority(0), QObject(parent) {
  qDebug() << "new role" << role_name;
}

QSharedPointer<Role> Role::create() {
  auto role = QSharedPointer<Role>(new Role());
  if (g::mainThread != QThread::currentThread())
    role->moveToThread(g::mainThread);
  return role;
}

QSharedPointer<Role> Role::create_from_db(
    const QUuid &id,
    const QUuid &server_id,
    const QByteArray &name,
    const QUuid &icon,
    const int color,
    const int priority,
    const QDateTime &creation) {
  if (const auto ptr = get_by_uid(id); !ptr.isNull())
    return ptr;

  auto role = QSharedPointer<Role>(new Role(name));
  if (g::mainThread != QThread::currentThread())
    role->moveToThread(g::mainThread);

  role->setUID(id);
  role->setServerUID(server_id);
  role->setName(name);
  role->setIconUID(icon);
  role->setColor(color);
  role->setPriority(priority);
  role->creation_date = creation;

  g::ctx->role_insert_cache(role);
  return role;
}

void Role::setUID(const QUuid &uid) {
  QWriteLocker locker(&mtx_lock);
  m_uid = uid;
  m_uid_str = uid.toString(QUuid::WithoutBraces).toUtf8();
}

QUuid Role::uid() const {
  QReadLocker locker(&mtx_lock);
  return m_uid;
}

QByteArray Role::name() const {
  QReadLocker locker(&mtx_lock);
  return m_name;
}

void Role::setName(const QByteArray &name) {
  QWriteLocker locker(&mtx_lock);
  m_name = name;
}

QUuid Role::server_uid() const {
  QReadLocker locker(&mtx_lock);
  return m_server_uid;
}

void Role::setServerUID(const QUuid &server_uid) {
  QWriteLocker locker(&mtx_lock);
  m_server_uid = server_uid;
}

QUuid Role::icon_uid() const {
  QReadLocker locker(&mtx_lock);
  return m_icon_uid;
}

void Role::setIconUID(const QUuid &icon_uid) {
  QWriteLocker locker(&mtx_lock);
  m_icon_uid = icon_uid;
}

int Role::color() const {
  QReadLocker locker(&mtx_lock);
  return m_color;
}

void Role::setColor(int color) {
  QWriteLocker locker(&mtx_lock);
  m_color = color;
}

int Role::priority() const {
  QReadLocker locker(&mtx_lock);
  return m_priority;
}

void Role::setPriority(int priority) {
  QWriteLocker locker(&mtx_lock);
  m_priority = priority;
}

QSharedPointer<Role> Role::get_by_uid(const QUuid &uid) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->roles_lookup_uuid.value(uid);
}

QSharedPointer<Role> Role::get_by_name(const QByteArray &name) {
  QReadLocker locker(&g::ctx->mtx_cache);
  return g::ctx->roles_lookup_name.value(name);
}

Role::~Role() {
  qDebug() << "RIP role" << m_name;
}

QVariantMap Role::to_variantmap() const {
  QReadLocker locker(&mtx_lock);
  QVariantMap map;
  map["uid"] = m_uid;
  map["name"] = QString::fromUtf8(m_name);
  map["server_uid"] = m_server_uid;
  map["icon_uid"] = m_icon_uid;
  map["color"] = m_color;
  map["priority"] = m_priority;
  map["creation_date"] = creation_date.toString(Qt::ISODate);
  return map;
}

rapidjson::Value Role::to_rapidjson(rapidjson::Document::AllocatorType &allocator) const {
  QReadLocker locker(&mtx_lock);
  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("uid", rapidjson::Value(m_uid_str.constData(), allocator), allocator);
  obj.AddMember("name", rapidjson::Value(m_name.constData(), allocator), allocator);
  obj.AddMember("server_uid", rapidjson::Value(m_server_uid.toString(QUuid::WithoutBraces).toUtf8().constData(), allocator), allocator);
  obj.AddMember("icon_uid", rapidjson::Value(m_icon_uid.toString(QUuid::WithoutBraces).toUtf8().constData(), allocator), allocator);
  obj.AddMember("color", m_color, allocator);
  obj.AddMember("priority", m_priority, allocator);
  obj.AddMember("creation_date", rapidjson::Value(creation_date.toString(Qt::ISODate).toUtf8().constData(), allocator), allocator);
  return obj;
}
