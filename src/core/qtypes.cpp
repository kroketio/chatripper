#include "qtypes.h"

#include "core/account.h"
#include "core/channel.h"
#include "core/permission.h"
#include "core/role.h"
#include "core/upload.h"

#include "ctx.h"

// QEventChannelJoin
QSharedPointer<QObject> QEventChannelJoin::getChannel() const {
  return qSharedPointerCast<QObject>(channel);
}

void QEventChannelJoin::setChannel(const QSharedPointer<QObject>& c) {
  channel = qSharedPointerCast<Channel>(c);
}

QSharedPointer<QObject> QEventChannelJoin::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventChannelJoin::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}

// QEventMessage
QSharedPointer<QObject> QEventMessage::getChannel() const {
  return qSharedPointerCast<QObject>(channel);
}

void QEventMessage::setChannel(const QSharedPointer<QObject>& c) {
  channel = qSharedPointerCast<Channel>(c);
}

QSharedPointer<QObject> QEventMessage::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventMessage::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}
