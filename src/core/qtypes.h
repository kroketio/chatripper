#pragma once
#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QVariant>
#include <QSharedPointer>
#include <QMap>
#include <QStringList>

class Channel;
class Account;

class QEventBase {
  Q_GADGET
  Q_PROPERTY(QByteArray reason MEMBER reason)
  Q_PROPERTY(bool _cancel MEMBER _cancel)
public:
  // t:str
  QByteArray reason;
  // t:bool d:False
  bool _cancel = false;

  QEventBase() = default;
  virtual ~QEventBase() = default;

  [[nodiscard]] bool cancelled() const { return _cancel; }
};

class QEventChannelJoin final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QSharedPointer<QObject> channel READ getChannel WRITE setChannel)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QByteArray password MEMBER password)
  Q_PROPERTY(bool from_system MEMBER from_system)
public:
  QSharedPointer<Channel> channel;
  QSharedPointer<Account> account;
  // t:str
  QByteArray password;

  // t:bool d:False
  bool from_system = false;

  QEventChannelJoin() = default;

  QSharedPointer<QObject> getChannel() const;
  void setChannel(const QSharedPointer<QObject>& c);

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);
};

class QEventMessage final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QByteArray id MEMBER id)
  Q_PROPERTY(QMap<QString, QVariant> tags MEMBER tags)
  Q_PROPERTY(QByteArray nick MEMBER nick)
  Q_PROPERTY(QByteArray host MEMBER host)
  Q_PROPERTY(QByteArray text MEMBER text)
  Q_PROPERTY(QByteArray user MEMBER user)
  Q_PROPERTY(QStringList targets MEMBER targets)
  Q_PROPERTY(QByteArray raw MEMBER raw)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QSharedPointer<QObject> channel READ getChannel WRITE setChannel)
  Q_PROPERTY(bool from_system MEMBER from_system)
public:
  QByteArray id;
  // t:Dict[str,Any] d:field(default_factory=dict)
  QMap<QString, QVariant> tags;
  // t:str
  QByteArray nick;
  // t:str
  QByteArray host;
  // t:str
  QByteArray text;
  // t:str
  QByteArray user;
  // t: List[str] d:field(default_factory=list)
  QStringList targets;

  QByteArray raw;

  // t:Account d:None
  QSharedPointer<Account> account;
  // t:Channel d:None
  QSharedPointer<Channel> channel;

  // t:bool d:False
  bool from_system = false;

  QEventMessage() = default;

  QSharedPointer<QObject> getChannel() const;
  void setChannel(const QSharedPointer<QObject>& c);

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);
};

class QEventAuthUser final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QByteArray username MEMBER username)
  Q_PROPERTY(QByteArray password MEMBER password)
  Q_PROPERTY(QString ip MEMBER ip)
  Q_PROPERTY(bool from_system MEMBER from_system)
public:
  // t:str
  QByteArray username;
  // t:str
  QByteArray password;
  QString ip;

  // t:bool d:False
  bool from_system = false;

  QEventAuthUser() = default;
};

class QEnums {
  Q_GADGET
public:
  enum class QModuleType : int {
    MODULE = 1 << 0,
    BOT    = 1 << 1
  };
  Q_ENUM(QModuleType)

  enum class QModuleMode : int {
    CONCURRENT = 1 << 0,
    EXCLUSIVE  = 1 << 1
  };
  Q_ENUM(QModuleMode)

  enum class QIRCEvent : int {
    AUTH_SASL_PLAIN = 1 << 0,
    CHANNEL_MSG     = 1 << 1,
    PRIVATE_MSG     = 1 << 2,
    CHANNEL_JOIN    = 1 << 3,
    CHANNEL_LEAVE   = 1 << 4
  };
  Q_ENUM(QIRCEvent)
};

struct ModuleHandler {
  Q_GADGET
  Q_PROPERTY(QEnums::QIRCEvent event MEMBER event)
  Q_PROPERTY(QString method MEMBER method)
public:
  QEnums::QIRCEvent event;
  QString method;
};

Q_DECLARE_METATYPE(QSharedPointer<QEventBase>)
Q_DECLARE_METATYPE(QSharedPointer<QEventChannelJoin>)
Q_DECLARE_METATYPE(QSharedPointer<QEventMessage>)
Q_DECLARE_METATYPE(QSharedPointer<QEventAuthUser>)
