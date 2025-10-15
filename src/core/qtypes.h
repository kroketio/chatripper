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

class QEventChannelRename final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QSharedPointer<QObject> channel READ getChannel WRITE setChannel)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QByteArray old_name MEMBER old_name)
  Q_PROPERTY(QByteArray new_name MEMBER new_name)
  Q_PROPERTY(QByteArray message MEMBER message)
public:
  // t:str
  QByteArray old_name;
  // t:str
  QByteArray new_name;
  // t:str
  QByteArray message;

  QSharedPointer<Account> account;
  QSharedPointer<Channel> channel;

  QSharedPointer<QObject> getChannel() const;
  void setChannel(const QSharedPointer<QObject>& c);

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);

  QEventChannelRename() = default;
};

class QEventPeerMaxConnections final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(int connections MEMBER connections)
  Q_PROPERTY(QString ip MEMBER ip)
public:
  int connections;
  QString ip;

  QEventPeerMaxConnections() = default;
};

class QEventNickChange final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QByteArray old_nick MEMBER old_nick)
  Q_PROPERTY(QByteArray new_nick MEMBER new_nick)
  Q_PROPERTY(bool from_server MEMBER from_server)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
public:
  // t:str
  QByteArray old_nick;
  // t:str
  QByteArray new_nick;

  // t:bool d:False
  bool from_server = false;

  QSharedPointer<Account> account;

  QEventNickChange() = default;

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);
};

class QEventRawMessage final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QByteArray raw MEMBER raw)
  Q_PROPERTY(QString ip MEMBER ip)
public:
  QByteArray raw;
  QString ip;

  QEventRawMessage() = default;
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

class QEventChannelPart final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QSharedPointer<QObject> channel READ getChannel WRITE setChannel)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QByteArray message MEMBER message)
  Q_PROPERTY(bool from_system MEMBER from_system)
public:
  QSharedPointer<Channel> channel;
  QSharedPointer<Account> account;

  // t:str d:""
  QByteArray message = "";

  // t:bool d:False
  bool from_system = false;

  QEventChannelPart() = default;

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
  Q_PROPERTY(QByteArray conn_id MEMBER conn_id)
  Q_PROPERTY(QStringList targets MEMBER targets)
  Q_PROPERTY(QByteArray raw MEMBER raw)
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QSharedPointer<QObject> dest READ getDest WRITE setDest)
  Q_PROPERTY(QSharedPointer<QObject> channel READ getChannel WRITE setChannel)
  Q_PROPERTY(bool from_system MEMBER from_system)
  Q_PROPERTY(bool tag_msg MEMBER tag_msg)
public:
  QByteArray id;
  QByteArray conn_id;
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
  // t:Account d:None
  QSharedPointer<Account> dest;
  // t:Channel d:None
  QSharedPointer<Channel> channel;

  // t:bool d:False
  bool from_system = false;
  // t:bool d:False
  bool tag_msg = false;

  QEventMessage() = default;

  QSharedPointer<QObject> getChannel() const;
  void setChannel(const QSharedPointer<QObject>& c);

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);

  QSharedPointer<QObject> getDest() const;
  void setDest(const QSharedPointer<QObject>& a);
};

class QEventMessageTags final : public QEventBase {
  Q_GADGET
  Q_PROPERTY(QSharedPointer<QObject> account READ getAccount WRITE setAccount)
  Q_PROPERTY(QMap<QString, QVariant> tags MEMBER tags)
  Q_PROPERTY(bool from_system MEMBER from_system)
public:
  // t:Account d:None
  QSharedPointer<Account> account;

  // t:Dict[str,Any] d:field(default_factory=dict)
  QMap<QString, QVariant> tags;

  QByteArray line;

  // t:bool d:False
  bool from_system = false;

  QSharedPointer<QObject> getAccount() const;
  void setAccount(const QSharedPointer<QObject>& a);

  QEventMessageTags() = default;
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
    AUTH_SASL_PLAIN       = 1 << 0,
    CHANNEL_MSG           = 1 << 1,
    PRIVATE_MSG           = 1 << 2,
    CHANNEL_JOIN          = 1 << 3,
    CHANNEL_PART          = 1 << 4,
    RAW_MSG               = 1 << 5,
    PEER_MAX_CONNECTIONS  = 1 << 6,
    NICK_CHANGE           = 1 << 7,
    CHANNEL_RENAME        = 1 << 8,
    TAG_MSG               = 1 << 9,
    VERIFY_MSG_TAGS       = 1 << 10
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
Q_DECLARE_METATYPE(QSharedPointer<QEventRawMessage>)
Q_DECLARE_METATYPE(QSharedPointer<QEventPeerMaxConnections>)
Q_DECLARE_METATYPE(QSharedPointer<QEventChannelPart>)
Q_DECLARE_METATYPE(QSharedPointer<QEventNickChange>)
Q_DECLARE_METATYPE(QSharedPointer<QEventChannelRename>)
Q_DECLARE_METATYPE(QSharedPointer<QEventMessageTags>)