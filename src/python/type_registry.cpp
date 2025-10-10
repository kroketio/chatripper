#include "core/account.h"
#include "core/channel.h"
#include "core/qtypes.h"

#include "type_registry.h"

QHash<QString, RegisteredType> PyTypeRegistry::registry_;

void PyTypeRegistry::registerType(
    const QString &pyName,
    const QMetaObject *meta,
    std::function<const QEventBase*(const QEventBase*)> castFunc
) {
  if (!meta) return;
  registry_.insert(pyName, {meta, pyName, castFunc});
}

void PyTypeRegistry::registerAll() {
  registerType("Account", &Account::staticMetaObject);
  registerType("Channel", &Channel::staticMetaObject);
  registerType("AuthUser", &QEventAuthUser::staticMetaObject,
      [](const QEventBase* ev) -> const QEventBase* {
          return dynamic_cast<const QEventAuthUser*>(ev);
      });

  registerType("Message", &QEventMessage::staticMetaObject,
      [](const QEventBase* ev) -> const QEventBase* {
          return dynamic_cast<const QEventMessage*>(ev);
      });

  registerType("ChannelJoin", &QEventChannelJoin::staticMetaObject,
      [](const QEventBase* ev) -> const QEventBase* {
          return dynamic_cast<const QEventChannelJoin*>(ev);
      });

  registerType("RawMessage", &QEventRawMessage::staticMetaObject,
      [](const QEventBase* ev) -> const QEventBase* {
          return dynamic_cast<const QEventRawMessage*>(ev);
      });
}

const RegisteredType *PyTypeRegistry::findByPyName(const QString &pyName) {
  auto it = registry_.find(pyName);
  return it != registry_.end() ? &it.value() : nullptr;
}

const QMetaObject* PyTypeRegistry::metaForPy(const QString& pyTypeName) {
  const auto it = all().find(pyTypeName);
  if (it != all().end()) return it->meta;
  return nullptr;
}

const RegisteredType *PyTypeRegistry::findByMeta(const QMetaObject *meta) {
  for (auto it = registry_.begin(); it != registry_.end(); ++it) {
    if (it.value().meta == meta)
      return &it.value();
  }
  return nullptr;
}

const QHash<QString, RegisteredType> &PyTypeRegistry::all() {
  return registry_;
}
