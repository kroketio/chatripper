#include <QHostAddress>
#include <QDateTime>
#include <QMutexLocker>

#include "irc/threaded_server.h"
#include "irc/client_connection.h"

#include "ctx.h"
#include "core/channel.h"
#include "core/account.h"
#include "lib/globals.h"
#include "irc/utils.h"

namespace irc {
  constexpr static qint64 CHUNK_SIZE = 1024;

  void client_connection::init() {
    const QUuid uuid = QUuid::createUuid();
    m_uid = uuid.toRfc4122();

    setup_tasks.set(
        ConnectionSetupTasks::CAP_EXCHANGE,
        ConnectionSetupTasks::NICK,
        ConnectionSetupTasks::USER);

    m_available_modes_count = static_cast<unsigned int>(UserModes::COUNT);
    m_time_connection_established = QDateTime::currentSecsSinceEpoch();

    // m_inactivityTimer = new QTimer(this);
    // m_inactivityTimer->setSingleShot(true);
    // connect(m_inactivityTimer, &QTimer::timeout, this, [this]{
    //     onSocketDisconnected();
    // });
    // m_inactivityTimer->start(MAX_INACTIVITY_MS);

    // m_host = socket->peerAddress().toString().toUtf8();
    m_host = g::defaultHost;

    connect(this, &client_connection::sendData, this, &client_connection::onWrite);
  }

  client_connection::client_connection(
      ThreadedServer* server, QWebSocket* socket, QObject *parent) : QObject(parent), m_websocket(socket), m_server(server) {
    init();

    m_websocket = socket;
    connect(m_websocket, &QWebSocket::binaryMessageReceived, this, &client_connection::parseIncomingWS);
    connect(m_websocket, &QWebSocket::textMessageReceived, [=](const QString& text) { this->parseIncomingWS(text.toUtf8()); });
  }

  client_connection::client_connection(
      ThreadedServer* server, QTcpSocket* socket, QObject *parent) : QObject(parent), m_socket(socket), m_server(server) {
    init();
  }

  void client_connection::handleConnection(const uint32_t peer_ip) {
    m_remote = QHostAddress(peer_ip);
    connect(m_socket, &QTcpSocket::readyRead, this, &client_connection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &client_connection::onSocketDisconnected);
  }

  void client_connection::handleWSConnection(const uint32_t peer_ip) {
    m_remote = QHostAddress(peer_ip);
    connect(m_websocket, &QWebSocket::disconnected, this, &client_connection::onSocketDisconnected);
  }

  void client_connection::handleCAP(const QList<QByteArray> &args) {
    if (!setup_tasks.has(ConnectionSetupTasks::CAP_EXCHANGE)) {
      // @TODO: support CAP after registration
      // send_raw("CAP * NAK :We already exchanged capabilities");
      return;
    }

    const auto &sub_cmd = args.at(0);
    static int client_cap_version = 0;

    if (sub_cmd == "LS") {
      bool support302 = false;
      if (args.size() >= 2) {
        bool ok = false;
        const int ver = args.at(1).toInt(&ok);
        if (ok && ver >= 302) {
          support302 = true;
          client_cap_version = std::max(client_cap_version, ver);
        }
      }

      QStringList caps = m_server->capabilities; // e.g. {"multi-prefix", "sasl=PLAIN,EXTERNAL"}

      if (support302) {
        const QByteArray reply = "CAP * LS :" + caps.join(' ').toUtf8();
        send_raw(reply);
      } else {
        QStringList simple;
        for (const QString &c: caps) {
          simple << c.section('=', 0, 0); // drop "=values"
        }
        const QByteArray reply = "CAP * LS :" + simple.join(' ').toUtf8();
        send_raw(reply);
      }
    } else if (sub_cmd == "REQ") {
      if (args.size() < 2) {
        send_raw("CAP * NAK :");
        return;
      }

      QString reqLine = QString::fromUtf8(args.at(1));
      if (reqLine.startsWith(':'))
        reqLine.remove(0, 1);

      QStringList requested = reqLine.split(' ', Qt::SkipEmptyParts);
      QStringList ack, nak;

      for (const QString &r: requested) {
        QString cap_name = r.startsWith('-') ? r.mid(1) : r;
        QString target = r;

        const bool available = std::any_of(
          m_server->capabilities.constBegin(),
          m_server->capabilities.constEnd(),
          [&](const QString &c) {
            return c.section('=', 0, 0) == cap_name;
          });

        if (available) {
          ack << target;
        } else {
          nak << target;
        }
      }

      if (!ack.isEmpty() && nak.isEmpty()) {
        const QByteArray reply = "CAP * ACK :" + ack.join(' ').toUtf8();
        send_raw(reply);

        for (const auto &cap: ack) {
          if (cap == "multi-prefix") {
            capabilities.set(PROTOCOL_CAPABILITY::MULTI_PREFIX);
          } else if (cap == "extended-join") {
            capabilities.set(PROTOCOL_CAPABILITY::EXTENDED_JOIN);
          } else if (cap == "chghost") {
            capabilities.set(PROTOCOL_CAPABILITY::CHGHOST);
          } else if (cap == "echo-message") {
            capabilities.set(PROTOCOL_CAPABILITY::ECHO_MESSAGE);
          } else if (cap == "znc.in/self-message") {
            capabilities.set(PROTOCOL_CAPABILITY::ZNC_SELF_MESSAGE);
          }
        }
      } else {
        const QByteArray reply = "CAP * NAK :" + requested.join(' ').toUtf8();
        send_raw(reply);
      }
    } else if (sub_cmd == "LIST") {
      QStringList enabled = m_server->capabilities;
      if (client_cap_version >= 302) {
        const QByteArray reply = "CAP * LIST :" + enabled.join(' ').toUtf8();
        send_raw(reply);
      } else {
        QStringList simple;
        for (const QString &c: enabled)
          simple << c.section('=', 0, 0);
        const QByteArray reply = "CAP * LIST :" + simple.join(' ').toUtf8();
        send_raw(reply);
      }
    } else if (sub_cmd == "END") {
      setup_tasks.clear(ConnectionSetupTasks::CAP_EXCHANGE);
      try_finalize_setup();
    } else if (sub_cmd == "NEW" || sub_cmd == "DEL") {
      if (client_cap_version >= 302) {
        // server sends these, client should not
      }
    }
  }

  void client_connection::handleMODE(const QList<QByteArray> &args) {
    if (args.size() == 0)
      return;

    const QByteArray target = args.at(0);

    auto _nick = nick();

    // --- MODE <target> (query) ---
    if (args.size() == 1) {
      // USER query
      if (target == _nick) {
        QString present;
        for (auto it = userModesLookup.constBegin(); it != userModesLookup.constEnd(); ++it) {
          const irc::UserModes mode = it.key();
          const QChar letter = it.value().letter;
          if (user_modes.has(mode))
            present += letter;
        }
        if (!present.isEmpty()) {
          send_raw("MODE " + _nick + " :" + present.toUtf8());
        } else {
          send_raw("MODE " + _nick + " :"); // empty
        }
        return;
      }

      // CHANNEL query
      if (target.startsWith('#')) {
        const auto channel = Channel::get(target.mid(1));
        if (!channel)
          return reply_num(403, target + " :No such channel");

        QString present;
        // collect letters for currently set channel modes
        for (auto it = channelModesLookup.constBegin(); it != channelModesLookup.constEnd(); ++it) {
          const irc::ChannelModes mode = it.key();
          const QChar letter = it.value().letter;
          if (channel->channel_modes.has(mode))
            present += letter;
        }

        // If there are parameters for certain modes, append them after the mode string
        QByteArray params;
        if (channel->channel_modes.has(irc::ChannelModes::KEY)) {
          params += " " + channel->key();
        }
        if (channel->channel_modes.has(irc::ChannelModes::LIMIT)) {
          params += " " + QByteArray::number(channel->limit());
        }

        if (!present.isEmpty()) {
          send_raw("324 " + _nick + " #" + channel->name() +" :" + ("+" + present).toUtf8() + params);
        } else {
          send_raw("324 " + _nick + " #" + channel->name() +" :");
        }
        return;
      }

      // otherwise unknown target
      return reply_num(501, "Unknown MODE flag");
    }

    // --- MODE <target> <modes...> (change request) ---
    QByteArray requested_modes = args.at(1);
    if (requested_modes.isEmpty() || (requested_modes[0] != '+' && requested_modes[0] != '-'))
      return reply_num(501, "Unknown MODE flag");

    const bool adding = requested_modes[0] == '+';
    QString result;

    // USER MODE change
    if (target == _nick) {
      bool invalid = false;
      for (int i = 1; i < requested_modes.size(); ++i) {
        QChar letter = requested_modes[i];
        if (!userModesLookupLetter.contains(letter)) {
          invalid = true;
          continue;
        }
        const auto mode = userModesLookupLetter.value(letter);
        const bool before = user_modes.has(mode);
        applyUserMode(mode, adding);
        const bool after = user_modes.has(mode);

        if (before != after)
          result += letter;
      }

      if (invalid)
        return reply_num(501, "Unknown MODE flag");

      if (!result.isEmpty()) {
        const QByteArray modePrefix = adding ? "+" : "-";
        send_raw("MODE " + _nick + " :" + modePrefix + result.toUtf8());
      } else {
        return reply_num(501, "Unknown MODE flag");
      }
      return;
    }

    // CHANNEL MODE change
    if (target.startsWith('#')) {
      const auto channel = Channel::get(target.mid(1));
      if (!channel)
        return reply_num(403, target + " :No such channel");

      bool invalid = false;
      int argIndex = 2; // args after mode string

      for (int i = 1; i < requested_modes.size(); ++i) {
        QChar letter = requested_modes[i];
        if (!channelModesLookupLetter.contains(letter)) {
          invalid = true;
          continue;
        }
        const auto mode = channelModesLookupLetter.value(letter);

        QByteArray modeArg;
        // these channel modes commonly take arguments
        if (mode == ChannelModes::BAN ||
            mode == ChannelModes::KEY ||
            mode == ChannelModes::LIMIT) {
          if (argIndex < args.size()) {
            modeArg = args.at(argIndex++);
          } else {
            // missing argument - treat as invalid for that mode
            // (choose to reply differently; mark invalid)
            invalid = true;
            continue;
          }
        }

        const bool before = channel->channel_modes.has(mode);
        channel->setMode(mode, adding, modeArg);
        const bool after = channel->channel_modes.has(mode);

        if (before != after)
          result += letter;
      }

      if (invalid)
        return reply_num(501, "Unknown MODE flag");

      if (!result.isEmpty()) {
        const QByteArray modePrefix = adding ? "+" : "-";
        send_raw("MODE " + target + " :" + modePrefix + result.toUtf8());
      }
      return;
    }

    // default
    return reply_num(501, "Unknown MODE flag");
  }

  void client_connection::applyUserMode(irc::UserModes mode, bool adding) {
    using irc::UserModes;

    switch (mode) {
      case UserModes::INVISIBLE:
        // e.g. update presence visibility for WHO/WHOIS
          // TODO: implement presence change
            break;

      case UserModes::CLOAK:
        // update host display for this connection
          break;

      case UserModes::REGISTERED:
        // if the user just registered/identified, maybe set some flags
          break;

      case UserModes::IRC_OPERATOR:
        // grant or revoke server operator privileges
          break;

      default:
        // other modes may not require immediate action
          break;
    }

    // finally update the bit in user_modes
    QWriteLocker locker(&mtx_lock);
    if (adding) user_modes.set(mode);
    else user_modes.clear(mode);
  }

  void client_connection::handlePASS(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(461, "PASS :Not enough parameters");
      return;
    }
    m_passGiven = args.first();
  }

  void client_connection::handleNICK(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(431, "No nickname given");
      return;
    }

    const auto &new_nick = args.first();

    // valid?
    if (!isValidNick(new_nick)) {
      reply_num(432, new_nick + " :Erroneous nickname");
      return;
    }

    // already active?
    const auto irc_nick_ptr = g::ctx->irc_nick_get(new_nick.toLower());
    if (!irc_nick_ptr.isNull()) {
      reply_num(433, new_nick + " :Nickname is already in use");
      return;
    }

    if (setup_tasks.has(ConnectionSetupTasks::NICK)) {
      setNick(new_nick);
      setup_tasks.clear(ConnectionSetupTasks::NICK);
      try_finalize_setup();
      return;
    }

    if (!setup_tasks.empty()) {
      reply_num(432, "Finish connect bootstrap first");
      return;
    }

    auto const account_nick = nick();

    // past connect bootstrap at this point
    if (new_nick == account_nick) {
      reply_num(431, "Your nick is already that");
      return;
    }

    const auto nick_change = QSharedPointer<QEventNickChange>(new QEventNickChange());
    nick_change->new_nick = args.first();
    nick_change->old_nick = account_nick;
    nick_change->account = m_account;

    if (!m_account->setNick(nick_change)) {
      reply_num(433, new_nick + " :Nickname is already in use");
    }
  }

  void client_connection::handleUSER(const QList<QByteArray> &args) {
    if (!setup_tasks.has(ConnectionSetupTasks::USER)) {
      reply_num(461, "USER :User already specified");
      return;
    }

    if (args.size() < 4) {
      reply_num(461, "USER :Not enough parameters");
      return;
    }

    auto &user_name = args[0];
    realname = args[3];
    user = user_name;

    if (user.length() > 16) {
      const QString msg = QString("USER :Your user is too long (more than 16 characters)").arg(user);
      reply_num(461, msg.toUtf8());
      forceDisconnect();
      return;
    }

    QReadLocker locker(&g::ctx->mtx_cache);
    if (g::ctx->account_username_exists(user))
      user_already_exists = true;
    locker.unlock();

    setup_tasks.clear(ConnectionSetupTasks::USER);
    try_finalize_setup();
  }

  void client_connection::channel_send_topic(const QByteArray &channel_name, const QByteArray &topic) {
    QReadLocker locker(&mtx_lock);
  }

  void client_connection::message(const QSharedPointer<QEventMessage> &message) {
    const bool cap_echo_message = capabilities.has(PROTOCOL_CAPABILITY::ECHO_MESSAGE);
    const bool cap_self_message = capabilities.has(PROTOCOL_CAPABILITY::ZNC_SELF_MESSAGE);
    auto const &src = message->account;
    QByteArray target;
    if (message->channel.isNull())
      target = message->dest->nick();
    else
      target = "#" + message->channel->name();

    QByteArray _prefix;
    if (src == m_account) {
      if (cap_echo_message)
        _prefix = prefix();
      else if (cap_self_message && m_uid != message->conn_id) {
        _prefix = prefix();
      } else {
        return;
      }
    } else {
      _prefix = src->prefix();
    }

    const QByteArray tag_prefix = buildTagPrefix(message, src, capabilities);

    const QByteArray msg =
        tag_prefix +
        ":" + _prefix +
        " PRIVMSG " + target +
        " :" + message->text + "\r\n";

    emit sendData(msg);
  }

  void client_connection::channel_join(const QSharedPointer<QEventChannelJoin> &event) {
    if (event.isNull()) return;

    const auto &account = event->account;
    const auto &channel = event->channel;

    if (channel.isNull()) {
      qCritical() << "no channel, DEBUG ME";
      return;
    }

    const auto channel_name = channel->name();

    // notification of a participant joining
    if (event->account != m_account) {
      const auto acc_prefix = account->prefix();

      const QByteArray msg = ":" + acc_prefix + " JOIN :#" + channel->name() + "\r\n";
      emit sendData(msg);

      QWriteLocker locker(&mtx_lock);
      if (!channel_members.contains(channel))
        channel_members[channel] = {};
      channel_members[channel] << account;
      locker.unlock();

      return;
    }

    // we are joining
    auto account_nick = nick();

    // this connection is already in the channel
    // @TODO: move `channels` mutations to setters/getters+lock
    QWriteLocker locker(&mtx_lock);
    if (channels.contains(channel_name))
      return;
    if (!channel_members.contains(channel))
      channel_members[channel] = {};
    for (const auto& member : channel->members()) {
      channel_members[channel] << member;
    }

    channels[channel_name] = channel;
    locker.unlock();

    reply_self("JOIN", ":#" + channel_name);

    // topic
    if (channel->topic().isEmpty()) {
      reply_num(331, "#" + channel_name + " :No topic is set");
    } else {
      // :irc.local 333 bla #test bbb!dsc@127.0.0.1 :1758051783.
      // @TODO: implement RPL_TOPICWHOTIME^
      send_raw("332 " + account_nick + " #" + channel_name + " :" + channel->topic());
    }

    // names
    // @TODO: use channel_members
    QByteArrayList names;
    for (auto &acc: channel->members()) {
      auto nick_acc = acc->nick();
      if (nick_acc.isEmpty())
        names << acc->name();
      else
        names << acc->nick();
    }

    const QByteArray namesPrefix = "353 " + account_nick + " = " + channel->name() + " :";
    send_raw(namesPrefix + names.join(" "));
    send_raw("366 " + account_nick + " " + "#" + channel->name() + " :End of NAMES list");
  }

  void client_connection::channel_part(const QSharedPointer<QEventChannelPart> &event) {
    const auto channel_name = event->channel->name();

    if (event->account->uid() == m_account->uid()) {
      // PART self
      QWriteLocker locker(&mtx_lock);
      channel_members.remove(event->channel);
      channels.remove(channel_name);
      locker.unlock();
      reply_self("PART", ":#" + channel_name);
    } else {
      // notify channel participants
      QReadLocker rlock(&mtx_lock);
      if (!channel_members.contains(event->channel))
        return;

      if (!channel_members[event->channel].contains(event->account))
        return;
      rlock.unlock();

      const auto acc_prefix = event->account->prefix();
      const QByteArray reason = event->message.isEmpty() ? "" : " :" + event->message;
      const QByteArray msg = ":" + acc_prefix + " PART #" + channel_name + reason + "\r\n";
      emit sendData(msg);

      QWriteLocker locker(&mtx_lock);
      if (channel_members.contains(event->channel))
        channel_members.remove(event->channel);
    }
  }

  void client_connection::handleJOIN(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(461, "JOIN :Not enough parameters");
      return;
    }

    QList<QByteArray> chans = args[0].split(',');
    for (auto& name : chans) {
      const auto channel_name = name.mid(1);
      if (!channel_name.isEmpty()) {

        const auto event = QSharedPointer<QEventChannelJoin>(new QEventChannelJoin);
        event->from_system = false;
        event->account = m_account;

        const auto channel = Channel::get_or_create(channel_name);
        if (!channel.isNull()) {
          event->channel = channel;
          channel->join(event);
        } else {
          reply_num(476, args.at(0) + " :Invalid channel name");
        }
      }
    }
  }

  void client_connection::handlePART(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(461, "PART :Not enough parameters");
      return;
    }

    const auto _nick = nick();

    // channels list
    auto chans = args.at(0).split(',');

    // optional part message
    QByteArray message;
    if (args.size() > 1) {
      message = args.at(1);
    }

    for (auto &_name: chans) {
      if (!_name.startsWith("#"))
        continue;

      auto channel_name = _name.mid(1);
      auto chan_ptr = Channel::get(channel_name);
      if (!chan_ptr.isNull()) {
        const auto event = QSharedPointer<QEventChannelPart>(new QEventChannelPart);
        event->from_system = false;
        event->channel = chan_ptr;
        event->account = m_account;
        event->message = message;
        if (chan_ptr->part(event))
          return;

      }

      send_raw("442 " + _nick + " " + channel_name + " :You're not on that channel");
    }
  }

  void client_connection::handlePRIVMSG(const QList<QByteArray> &args) {
    if (args.size() < 2) {
      reply_num(461, "PRIVMSG :Not enough parameters");
      return;
    }

    const auto _nick = nick();
    const QByteArray target = args[0];
    const QByteArray text = args[1];

    auto msg = QSharedPointer<QEventMessage>(new QEventMessage);
    msg->account = m_account;
    msg->conn_id = m_uid;
    msg->text = text;
    msg->from_system = false;
    msg->nick = _nick;
    msg->raw = args.join(" ");
    msg->user = user;
    msg->host = m_host;

    if (target.startsWith('#')) {
      const auto chan_ptr = Channel::get(target.mid(1));
      if (chan_ptr.isNull()) {
        send_raw("401 " + _nick + " " + target + " :No such nick/channel");
        return;
      }

      msg->channel = chan_ptr;
      chan_ptr->message(this, msg);
    } else {
      const auto dest = g::ctx->irc_nick_get(target);
      if (dest.isNull()) {
        send_raw("401 " + _nick + " " + target + " :No such nick/channel");
        return;
      }

      msg->dest = dest;
      m_account->message(this, msg);
    }
  }

  void client_connection::handleQUIT(const QList<QByteArray> &args) {
    const QByteArray reason = args.isEmpty() ? QByteArray("Client Quit") : args[0];
    // QByteArray line = ":" + prefix() + " QUIT :" + reason + "\r\n";

    // // broadcast quits
    // for (Channel *ch: std::as_const(channels)) {
    //   for (auto const &acc: ch->members()) {
    //     for (const auto &c: acc->connections()) {
    //       if (c != this) {
    //         c->write(line);
    //       }
    //     }
    //   }
    //   ch->remove(m_account);
    //   m_server->removeChannelIfEmpty(ch);
    // }
    forceDisconnect();
  }

  void client_connection::handleRENAME(const QList<QByteArray> &args) {
    if (args.isEmpty() || args.size() <= 1)
      return;

    QByteArray message;
    auto from_channel = args.first();
    auto to_channel = args.at(1);

    if (from_channel.startsWith("#"))
      from_channel = from_channel.mid(1);

    if (to_channel.startsWith("#"))
      to_channel = to_channel.mid(1);

    auto channel_from = Channel::get(from_channel);
    auto channel_to = Channel::get(to_channel);

    if (channel_from.isNull()) {
      // :irc.example.com FAIL RENAME CANNOT_RENAME #global %local :Some error
      QByteArrayList msg;
      msg << ":" + prefix();
      msg << "FAIL RENAME CANNOT_RENAME";
      msg << "#" + from_channel;
      msg << "#" + to_channel;
      msg << ":Channel to rename does not exist\r\n";

      emit sendData(msg.join(" "));
      return;
    }

    if (!channel_to.isNull()) {
      // :irc.example.com FAIL RENAME CHANNEL_NAME_IN_USE #evil #good :Channel already exists
      QByteArrayList msg;
      msg << ":" + prefix();
      msg << "FAIL RENAME CHANNEL_NAME_IN_USE";
      msg << "#" + from_channel;
      msg << "#" + to_channel;
      msg << ":Channel already exists\r\n";

      emit sendData(msg.join(" "));
      return;
    }

    if (args.size() > 2)
      message = args.at(2);

    const auto rename = QSharedPointer<QEventChannelRename>(new QEventChannelRename);
    rename->old_name = from_channel;
    rename->new_name = to_channel;
    rename->account = m_account;
    rename->message = message;
    rename->channel = channel_from;
    Channel::rename(rename);
  }

  // @TODO: error replies
  void client_connection::handleCHATHISTORY(const QList<QByteArray> &args) {
    if (args.isEmpty() || args.size() <= 3)
      return;

    auto channel_name = args.at(1).mid(1);

    auto chan_ptr = Channel::get(channel_name);
    if (chan_ptr.isNull())
      return;

    send_raw("BATCH +123 chathistory #" + chan_ptr->name());
    send_raw("BATCH -123");
  }

  void client_connection::handleNAMES(const QList<QByteArray> &args) {
    // if (args.isEmpty()) {
    //   // list names for all joined channels
    //   for (Channel *ch: std::as_const(channels)) {
    //     QByteArray names;
    //     for (auto const &acc: ch->members()) {
    //       for (const auto &c: acc->connections()) {
    //         if (!names.isEmpty()) {
    //           names += " ";
    //         }
    //         names += c->nickname();
    //       }
    //     }
    //
    //     sendRaw("353 " + nick + " = " + ch->name() + " :" + names);
    //     sendRaw("366 " + nick + " " + ch->name() + " :End of NAMES list");
    //   }
    // } else {
    //   QList<QByteArray> chans = args[0].split(',');
    //   for (auto &c: chans) {
    //
    //     const Channel *ch = nullptr;
    //     for (const Channel *mine: std::as_const(channels)) {
    //       if (ircLower(mine->name()) == ircLower(c)) {
    //         ch = mine;
    //         break;
    //       }
    //     }
    //
    //     if (!ch) {
    //       sendRaw("403 " + nick + " " + c + " :No such channel");
    //       continue;
    //     }
    //
    //     QByteArray names;
    //     for (auto const &acc: ch->members()) {
    //       for (const auto &cc: acc->connections()) {
    //         if (!names.isEmpty()) {
    //           names += " ";
    //         }
    //         names += cc->nickname();
    //       }
    //     }
    //
    //     sendRaw("353 " + nick + " = " + ch->name() + " :" + names);
    //     sendRaw("366 " + nick + " " + ch->name() + " :End of NAMES list");
    //   }
    // }
  }

  void client_connection::handleTOPIC(const QList<QByteArray> &args) {
    // if (args.isEmpty()) {
    //   replyNumeric(461, nick, "TOPIC :Not enough parameters");
    //   return;
    // }
    //
    // const QByteArray& chan = args[0];
    // Channel *ch = nullptr;
    // for (Channel *mine: std::as_const(channels)) {
    //   if (ircLower(mine->name()) == ircLower(chan)) {
    //     ch = mine;
    //     break;
    //   }
    // }
    //
    // if (!ch) {
    //   sendRaw("442 " + nick + " " + chan + " :You're not on that channel");
    //   return;
    // }
    //
    // if (args.size() == 1) {
    //   if (ch->topic().isEmpty())
    //     sendRaw("331 " + nick + " " + ch->name() + " :No topic is set");
    //   else
    //     sendRaw("332 " + nick + " " + ch->name() + " :" + ch->topic());
    // } else {
    //   ch->setTopic(args[1]);
    //   const QByteArray line = ":" + prefix() + " TOPIC " + ch->name() + " :" + ch->topic() + "\r\n";
    //
    //   for (auto const &acc: ch->members()) {
    //     for (const auto &c: acc->connections()) {
    //       c->write(line);
    //     }
    //   }
    // }
  }

  void client_connection::handleLUSERS(const QList<QByteArray> &args) {
    QReadLocker rlock(&g::ctx->mtx_cache);
    unsigned int count_users = g::ctx->accounts.size();
    rlock.unlock();
    unsigned int count_peers = m_server->concurrent_peers();

    // LUSERS 251
    QByteArrayList msg_251;
    msg_251 << "251";
    msg_251 << nick();
    msg_251 << "There are";
    msg_251 << QByteArray::number(count_users) << "users,";
    msg_251 << QByteArray::number(count_peers) << "connected peers,";
    msg_251 << "and 0 services on 1 server(s)\r\n";
    send_raw(msg_251.join(" "));

    // LUSERS 252
    QByteArrayList msg_252;
    msg_252 << "252";
    msg_252 << nick();
    msg_252 << "I have";
    msg_252 << QByteArray::number(count_users) << "users,";
    msg_252 << QByteArray::number(count_peers) << "connected peers";
    send_raw(msg_252.join(" "));
  }

  void client_connection::change_host(const QByteArray &new_host) {
    if (!setup_tasks.empty())
      return;

    // @TODO: needs a broadcast to all users in a channel that this m_nick is in
    // https://ircv3.net/specs/extensions/chghost
    // write(":" + prefix() + " CHGHOST " + m_nick + " " + new_host + "\r\n");
    // m_nick = new_host;
  }

  bool client_connection::channel_rename(const QSharedPointer<QEventChannelRename> &event) {
    if (event->old_name == event->new_name)
      return false;

    const auto from_channel = "#" + event->old_name;
    const auto to_channel = "#" + event->new_name;

    if (capabilities.has(PROTOCOL_CAPABILITY::CHANNEL_RENAME)) {
      QByteArray message;
      if (!event->message.isEmpty())
        message += ": " + event->message;

      const QByteArray msg = ":" + prefix() + " RENAME " + from_channel + " " + to_channel + message + "\r\n";
      emit sendData(msg);
      return true;
    }

    // for clients that do not support cap channel rename
    const QByteArray part_msg = ":" + prefix() + " PART " + "#" + from_channel + ":Changing the channel name" + "\r\n";
    emit sendData(part_msg);
    const QByteArray join_msg = ":" + prefix() + " JOIN " + "#" + to_channel + "\r\n";
    emit sendData(join_msg);
    return true;
  }

  bool client_connection::change_nick(const QSharedPointer<QEventNickChange> &event) {
    // 1. if we are ourselves
    //   - first check if we even need to change nick
    //   - same connection? use local prefix
    //   - from another connection? does not matter.
    // 2. if delivered to someone else
    //   - prefix(old_nick)

    const auto account_nick = nick();
    if (event->account == m_account) {
      if (m_nick == account_nick)
        return false;

      auto _prefix = event->old_nick + "!" + user + "@" + m_host;
      const QByteArray msg = ":" + _prefix + " NICK :" + event->new_nick + "\r\n";
      emit sendData(msg);
      return true;
    }

    auto _prefix = event->account->prefix(event->old_nick);
    const QByteArray msg = ":" + _prefix + " NICK :" + event->new_nick + "\r\n";
    emit sendData(msg);
    return true;
  }

  void client_connection::send_raw(const QByteArray &line) const {
    const QByteArray out = ":" + ThreadedServer::serverName() + " " + line + "\r\n";
    emit sendData(out);
  }

  void client_connection::change_host(const QSharedPointer<Account> &acc, const QByteArray &new_host) {

  }

  void client_connection::reply_num(const int code, const QByteArray &text) {
    auto const _nick = nick();
    const QByteArray target = _nick.isEmpty() ? "*" : _nick;
    const QByteArray line = QByteArray::number(code).rightJustified(3, '0') + " " + target + " :" + text;
    send_raw(line);
  }

  QByteArray client_connection::prefix() {
    QReadLocker rlock(&mtx_lock);
    auto const _nick = nick();
    return _nick + "!" + (user.isEmpty() ? QByteArray("user") : user) + "@" + m_host;
  }

  void client_connection::reply_self(const QByteArray &command, const QByteArray &args) {
    QByteArray line = ":" + prefix() + " " + command;
    if (!args.isEmpty())
      line += " " + args;
    line += "\r\n";
    emit sendData(line);
  }

  void client_connection::onReadyRead() {
    // @TODO: deal with clients sending data too fast - fakelag
    while (m_socket->bytesAvailable() > 0) {
      constexpr qint64 MAX_BUFFER_SIZE = 1024;

      QByteArray chunk = m_socket->read(qMin(m_socket->bytesAvailable(), CHUNK_SIZE));
      if (chunk.isEmpty())
        break;
      m_buffer.append(chunk);

      if (m_buffer.size() > MAX_BUFFER_SIZE) {
#ifndef QT_NO_DEBUG_OUTPUT
        qDebug() << "client sent too much data without newline, discarding buffer";
#endif
        m_buffer.clear();
        // @TODO: add to naughty clients list
        return onSocketDisconnected();
      }
    }

    while (true) {
      int n = m_buffer.indexOf('\n');
      if (n < 0)
        break;

      QByteArray raw = m_buffer.left(n);
      if (raw.endsWith('\r'))
        raw.chop(1);

      m_buffer.remove(0, n + 1);
      parseIncoming(raw);
    }
  }

  void client_connection::onSocketDisconnected() {
    auto const _nick = nick();
    if (!m_account.isNull())
      m_account->onConnectionDisconnected(this, _nick);
    emit disconnected(_nick);
  }

  void client_connection::try_finalize_setup() {
    if (is_ready || !setup_tasks.empty())
      return;

    const auto server_password = m_server->password();
    if (!server_password.isEmpty()) {
      if (m_passGiven.isEmpty()) {
        reply_num(464, "Password incorrect");
        return forceDisconnect();
      }

      if (m_passGiven != server_password) {
        reply_num(464, "Password incorrect");
        return forceDisconnect();
      }
    }

    if (m_account.isNull()) {
      m_account = Account::create();
      m_account->setNickByForce(m_nick);
      m_account->setRandomUID();
      g::ctx->account_insert_cache(m_account);
    }

    // sync nick (account has precedence)
    const auto account_nick = m_account->nick();
    if (account_nick != m_nick) {
      auto _prefix = m_nick + "!" + user + "@" + m_host;
      m_nick = account_nick;
      const QByteArray msg = ":" + _prefix + " NICK :" + account_nick + "\r\n";
      emit sendData(msg);
    }

    m_account->add_connection(this);
    auto const _nick = nick();

    // ensure it's in cache
    g::ctx->irc_nicks_insert_cache(_nick, m_account);

    reply_num(1, "Hi, welcome to IRC");
    reply_num(2, "Your host is " + ThreadedServer::serverName() + ", running version cIRCa-0.1");
    reply_num(3, "This server was created Dec 21 1989 at 13:37:00 (lie)");
    reply_num(4, ThreadedServer::serverName() + " wut-7.2.2+bla.7.3 what is this.");

    const QByteArray line = "005 " + _nick + " BOT=b CASEMAPPING=ascii CHANNELLEN=64 CHANTYPES=# ELIST=U EXCEPTS EXTBAN=,m :are supported by this server";
    send_raw(line);

    handleLUSERS({});
    handleMOTD({});

    if (logged_in)
      handleMODE({_nick, "+r"});

    for (const auto& channel : m_account->channels) {
      auto event = QSharedPointer<QEventChannelJoin>(new QEventChannelJoin);
      event->from_system = true;
      event->channel = channel;
      event->account = m_account;
      channel->join(event);
    }

    is_ready = true;
  }

  void client_connection::handleAUTHENTICATE(const QList<QByteArray> &args) {
    if (args.empty()) {
      send_raw("uwot?");
      forceDisconnect();
      return;
    }

    const auto& arg = args.at(0);
    if (arg == "PLAIN") {
      send_raw("AUTHENTICATE +");
      return;
    }

    const QByteArray plain = QByteArray::fromBase64(arg);
    const auto plain_spl = plain.split('\0');

    if (plain_spl.length() != 3) {
      reply_num(900, "SASL authentication failed");
      return;
    }

    const QByteArray& username = plain_spl.at(0);
    const QByteArray& password = plain_spl.at(1);

    const auto account = Account::get_by_name(username);
    if (!account.isNull()) {
      auto auth = QSharedPointer<QEventAuthUser>(new QEventAuthUser);
      auth->username = username;
      auth->password = password;
      auth->ip = get_ip();

      auth = account->verifyPassword(auth);
      if (!auth->cancelled()) {
        if (!m_account.isNull())
          account->merge(m_account);  // @TODO: fix this when we support SASL login *after* connection bootstrap
        else {
          m_account = account;
        }

        reply_num(900, "You are now logged in as " + username);
        reply_num(903, "SASL authentication successful");
        logged_in = true;
        return;
      }

      QByteArray reply = "SASL authentication failed";
      if (!auth->reason.isEmpty())
        reply += ": " + auth->reason;

      reply_num(900, reply);
      forceDisconnect();
      return;
    }

    reply_num(900, "SASL authentication failed");
    forceDisconnect();
  }

  void client_connection::forceDisconnect() const {
    if (m_websocket != nullptr)
      return m_websocket->close();
    m_socket->disconnectFromHost();
  }

  void client_connection::handleMOTD(const QList<QByteArray> &) {
    auto const _nick = nick();
    const QByteArray motd_text = m_server->motd().isEmpty() ? QByteArray("Welcome!") : m_server->motd();

    // MOTD end
    send_raw("375 " + _nick + " :- " + ThreadedServer::serverName() + " Message of the day -");

    const QList<QByteArray> lines = motd_text.split('\n');
    for (const QByteArray &rawLine : lines) {
      QByteArray line = rawLine.trimmed();

      // chunks
      int pos = 0;
      while (pos < line.size()) {
        constexpr int MAX_CONTENT_LEN = 400;
        QByteArray chunk = line.mid(pos, MAX_CONTENT_LEN);
        send_raw("372 " + _nick + " :" + chunk);
        pos += MAX_CONTENT_LEN;
      }
    }

    // MOTD end
    send_raw("376 " + _nick + " :End of MOTD command.");
  }

  // note, bot mode has different output: https://ircv3.net/specs/extensions/bot-mode
  void client_connection::handleWHO(const QList<QByteArray> &args) {
    if (args.size() < 1) {
      reply_num(461, "WHO :Not enough parameters");
      return;
    }

    // keep original argument for replies
    QByteArray raw_channel_arg = args[0];

    // channel name without '#' for internal lookup
    QByteArray channel_name = raw_channel_arg;
    if (channel_name.startsWith('#')) {
      channel_name = channel_name.mid(1);
    }

    auto chan_ptr = Channel::get(channel_name);
    if (!chan_ptr) {
      send_raw("401 " + nick() + " " + raw_channel_arg + " :No such nick/channel");
      return;
    }

    // iterate over members
    for (auto &acc : chan_ptr->members()) {
      QByteArray _nick = acc->nick();

      // host placeholder
      QByteArray host = acc->host();
      if (host.isEmpty()) host = g::defaultHost;

      // ident
      QByteArray ident = "~u";

      // status: H (online), G (offline)
      QByteArray status = acc->hasConnections() ? "H" : "G";
      // operator or not
      // @TODO: replace with actual permissions
      if (acc->name() == "admin")
        status += "@";

      QByteArray hopcount = "0";
      const QByteArray& realname = _nick;

      // WHO line
      QByteArrayList who_parts;
      who_parts.append("354");
      who_parts.append(nick());
      who_parts.append(raw_channel_arg);
      who_parts.append(ident);
      who_parts.append(host);
      who_parts.append(_nick);
      who_parts.append(status);
      who_parts.append(hopcount);
      who_parts.append("*");
      who_parts.append(realname);

      // WHO numeric 354 with server prefix
      send_raw(who_parts.join(" "));
    }

    // end of WHO list using QByteArrayList only
    QByteArrayList end_parts;
    end_parts.append("315");
    end_parts.append(nick());
    end_parts.append(raw_channel_arg);
    end_parts.append(":End of WHO list");
    send_raw(end_parts.join(" "));
  }

  void client_connection::handleWHOIS(const QList<QByteArray> &) {
    // note, bot mode has different output: https://ircv3.net/specs/extensions/bot-mode
  }

  void client_connection::handlePING(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(409, "No origin specified");
      return;
    }

    m_last_activity = QDateTime::currentSecsSinceEpoch();

    const QByteArray& token = args.last();
    const QByteArray out = "PONG " + ThreadedServer::serverName() + " :" + token + "\r\n";
    emit sendData(out);
  }

  void client_connection::onWrite(const QByteArray &data) const {
    if (m_websocket && m_websocket->isValid()) {
      qDebug() << ">" << data;
      m_websocket->sendTextMessage(data);
      return;
    }

    if (!m_socket || !m_socket->isOpen() || !m_socket->isWritable())
      return;

    m_socket->write(data);
  }

  void client_connection::handlePONG(const QList<QByteArray> &) {
    m_last_activity = QDateTime::currentSecsSinceEpoch();
  }

  QList<QByteArray> client_connection::split_irc(const QByteArray &line) {
    const int sp = line.indexOf(' ');
    QList<QByteArray> out;
    if (sp < 0) {
      out << line;
      return out;
    }

    const QByteArray cmd = line.left(sp);
    QByteArray rest = line.mid(sp + 1);
    QByteArray trailing;
    const int colon = rest.indexOf(" :");
    if (colon >= 0) {
      trailing = rest.mid(colon + 2);
      rest = rest.left(colon);
    }

    QList<QByteArray> params = rest.split(' ');
    out << cmd;
    for (auto &p: params)
      if (!p.isEmpty())
        out << p;
    if (!trailing.isEmpty())
      out << trailing;
    return out;
  }

  QByteArray client_connection::irc_lower(const QByteArray &s) {
    QByteArray out = s.toLower();
    out.replace('[', '{');
    out.replace(']', '}');
    out.replace('\\', '|');
    return out;
  }

  void client_connection::disconnect() const {
    forceDisconnect();
  }

  void client_connection::parseIncomingWS(QByteArray line) {
    qDebug() << line;
    return parseIncoming(line);
  }

  void client_connection::parseIncoming(QByteArray &line) {
    line = line.trimmed();
    if (line.isEmpty())
      return;

    if (g::ctx->snakepit->hasEventHandler(QEnums::QIRCEvent::RAW_MSG)) {
      auto raw = QSharedPointer<QEventRawMessage>(new QEventRawMessage());
      raw->raw = line;
      raw->ip = m_remote.toString();

      const auto result = g::ctx->snakepit->event(
        QEnums::QIRCEvent::RAW_MSG,
        raw);

      if (result.canConvert<QSharedPointer<QEventRawMessage>>()) {
        if (raw->cancelled())
          return;

        line = raw->raw;
      }
    }

    auto parts = split_irc(line);
    if (parts.isEmpty())
      return;

    const QByteArray cmd = parts.first();
    parts.pop_front();

    if (cmd == "PASS")
      handlePASS(parts);
    else if (cmd == "NICK")
      handleNICK(parts);
    else if (cmd == "USER")
      handleUSER(parts);
    else if (cmd == "PING")
      handlePING(parts);
    else if (cmd == "PONG")
      handlePONG(parts);
    else if (cmd == "JOIN" && is_ready) {
      if (parts.at(0) == ":" && parts.size() == 1)
        // replyNumeric(421, m_nick.isEmpty() ? QByteArray("*") : m_nick, "Unknown JOIN command");
          return;
      handleJOIN(parts);
    }
    else if (cmd == "PART" && is_ready)
      handlePART(parts);
    else if (cmd == "PRIVMSG" && is_ready)
      handlePRIVMSG(parts);
    else if (cmd == "QUIT")
      handleQUIT(parts);
    else if (cmd == "NAMES" && is_ready)
      handleNAMES(parts);
    else if (cmd == "CHATHISTORY" && is_ready)
      handleCHATHISTORY(parts);
    else if (cmd == "RENAME" && is_ready)
      handleRENAME(parts);
    else if (cmd == "TOPIC" && is_ready)
      handleTOPIC(parts);
    else if (cmd == "LUSERS" && is_ready)
      handleLUSERS(parts);
    else if (cmd == "MOTD")
      handleMOTD(parts);
    else if (cmd == "WHO" && is_ready)
      handleWHO(parts);
    else if (cmd == "AUTHENTICATE")
      handleAUTHENTICATE(parts);
    else if (cmd == "CAP")
      handleCAP(parts);
    else if (cmd == "MODE" && is_ready)
      handleMODE(parts);
    else {
      return;
      reply_num(421, "Unknown command");
    }

    m_last_activity = QDateTime::currentSecsSinceEpoch();
  }

  QByteArray client_connection::nick() {
    if (!m_account.isNull())
      return m_account->nick();
    return m_nick.isEmpty() ? "*" : m_nick;
  }

  QString client_connection::get_ip() const {
    if (m_websocket != nullptr)
      return m_websocket->peerAddress().toString();
    return m_socket->peerAddress().toString();
  }

  client_connection::~client_connection() {
    if (m_websocket != nullptr)
      m_websocket->close();
    else
      m_socket->deleteLater();
  }
}
