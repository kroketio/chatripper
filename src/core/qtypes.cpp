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

// QEventChannelPart
QSharedPointer<QObject> QEventChannelPart::getChannel() const {
  return qSharedPointerCast<QObject>(channel);
}

void QEventChannelPart::setChannel(const QSharedPointer<QObject>& c) {
  channel = qSharedPointerCast<Channel>(c);
}

QSharedPointer<QObject> QEventChannelPart::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventChannelPart::setAccount(const QSharedPointer<QObject>& a) {
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

QSharedPointer<QObject> QEventMessage::getDest() const {
  return qSharedPointerCast<QObject>(dest);
}

void QEventMessage::setDest(const QSharedPointer<QObject>& a) {
  dest = qSharedPointerCast<Account>(a);
}

// QEventNickChange

QSharedPointer<QObject> QEventNickChange::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventNickChange::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}

// QEventChannelRename
QSharedPointer<QObject> QEventChannelRename::getChannel() const {
  return qSharedPointerCast<QObject>(channel);
}

void QEventChannelRename::setChannel(const QSharedPointer<QObject>& c) {
  channel = qSharedPointerCast<Channel>(c);
}

QSharedPointer<QObject> QEventChannelRename::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventChannelRename::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}

// QEventMessageTags
QSharedPointer<QObject> QEventMessageTags::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventMessageTags::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}

// QEventMetadata.cpp

QSharedPointer<QObject> QEventMetadata::getAccount() const {
  return qSharedPointerCast<QObject>(account);
}

void QEventMetadata::setAccount(const QSharedPointer<QObject>& a) {
  account = qSharedPointerCast<Account>(a);
}

QSharedPointer<QObject> QEventMetadata::getDest() const {
  return qSharedPointerCast<QObject>(dest);
}

void QEventMetadata::setDest(const QSharedPointer<QObject>& d) {
  dest = qSharedPointerCast<Account>(d);
}

QSharedPointer<QObject> QEventMetadata::getChannel() const {
  return qSharedPointerCast<QObject>(channel);
}

void QEventMetadata::setChannel(const QSharedPointer<QObject>& c) {
  channel = qSharedPointerCast<Channel>(c);
}
