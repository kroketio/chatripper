#include <QHostAddress>
#include <QDateTime>
#include <QMutexLocker>

#include "irc/threaded_server.h"
#include "irc/client_connection.h"

#include "ctx.h"
#include "core/channel.h"
#include "core/account.h"
#include "lib/globals.h"

namespace irc {
  constexpr static qint64 CHUNK_SIZE = 1024;

  client_connection::client_connection(ThreadedServer* server, QTcpSocket* socket, QObject *parent) : QObject(parent), m_socket(socket), m_server(server) {
    setup_tasks.set(
        ConnectionSetupTasks::CAP_EXCHANGE,
        ConnectionSetupTasks::NICK,
        ConnectionSetupTasks::USER);

    m_available_modes_count = static_cast<unsigned int>(UserModes::COUNT);
    m_time_connection_established = QDateTime::currentSecsSinceEpoch();

    m_account = Account::create();
    m_account->setRandomUID();
    m_account->add_connection(this);
    g::ctx->account_insert_cache(m_account);

    // m_host = socket->peerAddress().toString().toUtf8();
    m_host = g::defaultHost;

    // m_inactivityTimer = new QTimer(this);
    // m_inactivityTimer->setSingleShot(true);
    // connect(m_inactivityTimer, &QTimer::timeout, this, [this]{
    //     onSocketDisconnected();
    // });
    // m_inactivityTimer->start(MAX_INACTIVITY_MS);

    connect(this, &client_connection::sendData, this, &client_connection::onWrite);
  }

  void client_connection::handleConnection(const QHostAddress &peer_ip) {
    m_remote = peer_ip;
    connect(m_socket, &QTcpSocket::readyRead, this, &client_connection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &client_connection::onSocketDisconnected);
  }

  // void wefe() {
  //   m_account = Account::create();
  //   m_account->setRandomUID();
  //   m_account->add_connection(this);
  //   g::ctx->account_insert_cache(m_account);
  //
  //   // m_host = socket->peerAddress().toString().toUtf8();
  //   m_host = g::defaultHost;
  // }

  void client_connection::handleCAP(const QList<QByteArray> &args) {
    if (!setup_tasks.has(ConnectionSetupTasks::CAP_EXCHANGE)) {
      send_raw("CAP * NAK :We already exchanged capabilities");
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

    // --- MODE <target> (query) ---
    if (args.size() == 1) {
      // USER query
      if (target == nick) {
        QString present;
        for (auto it = userModesLookup.constBegin(); it != userModesLookup.constEnd(); ++it) {
          const irc::UserModes mode = it.key();
          const QChar letter = it.value().letter;
          if (user_modes.has(mode))
            present += letter;
        }
        if (!present.isEmpty()) {
          send_raw("MODE " + nick + " :" + present.toUtf8());
        } else {
          send_raw("MODE " + nick + " :"); // empty
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
          send_raw("324 " + nick + " #" + channel->name() +" :" + ("+" + present).toUtf8() + params);
        } else {
          send_raw("324 " + nick + " #" + channel->name() +" :");
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
    if (target == nick) {
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
        send_raw("MODE " + nick + " :" + modePrefix + result.toUtf8());
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
    QMutexLocker locker(&mtx_lock);
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

    if (!m_account->setNick(args.at(0))) {
      reply_num(433, nick + " :Nickname is already in use");
    } else {
      setup_tasks.clear(ConnectionSetupTasks::NICK);
      try_finalize_setup();
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
      m_socket->disconnectFromHost();
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
    QMutexLocker locker(&mtx_lock);
  }

  void client_connection::self_message(const QByteArray& target, const QSharedPointer<QEventMessage> &message) const {
    if (capabilities.has(PROTOCOL_CAPABILITY::ZNC_SELF_MESSAGE)) {
      const QByteArray msg = ":" + prefix() + " PRIVMSG " + target + " :" + message->text + "\r\n";
      emit sendData(msg);
    }
  }

  void client_connection::message(const QSharedPointer<Account> &src, const QByteArray& target, const QSharedPointer<QEventMessage> &message) const {
    const QByteArray msg = ":" + src->prefix(0) + " PRIVMSG " + target + " :" + message->text + "\r\n";
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
      const auto acc_prefix = account->prefix(0);

      const QByteArray msg = ":" + acc_prefix + " JOIN :#" + channel->name() + "\r\n";
      emit sendData(msg);

      QMutexLocker locker(&mtx_lock);
      if (!channel_members.contains(channel))
        channel_members[channel] = {};
      channel_members[channel] << account;

      return;
    }

    // we are joining

    // this connection is already in the channel
    // @TODO: move `channels` mutations to setters/getters+lock
    QMutexLocker locker(&mtx_lock);
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
      send_raw("332 " + nick + " #" + channel_name + " :" + channel->topic());
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

    const QByteArray namesPrefix = "353 " + nick + " = " + channel->name() + " :";
    send_raw(namesPrefix + names.join(" "));
    send_raw("366 " + nick + " " + "#" + channel->name() + " :End of NAMES list");
  }

  void client_connection::channel_part(const QSharedPointer<Account> &account, const QSharedPointer<Channel> &channel, const QByteArray &message) {
    const auto acc_prefix = account->prefix(0);

    const QByteArray reason = message.isEmpty() ? "" : " :" + message;
    const QByteArray msg = ":" + acc_prefix + " PART #" + channel->name() + reason + "\r\n";
    emit sendData(msg);

    QMutexLocker locker(&mtx_lock);
    if (channel_members.contains(channel))
      channel_members.remove(channel);
  }

  void client_connection::channel_part(const QByteArray &channel_name, const QByteArray &message) {
    // local bookkeeping
    const auto ptr = Channel::get(channel_name);

    QMutexLocker locker(&mtx_lock);
    channel_members.remove(ptr);
    channels.remove(channel_name);
    locker.unlock();

    reply_self("PART", ":#" + channel_name);
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
        const auto channel = Channel::get(channel_name);
        if (!channel.isNull()) {
          const auto event = QSharedPointer<QEventChannelJoin>(new QEventChannelJoin);
          event->from_system = false;
          event->channel = channel;
          event->account = m_account;
          return channel->join(event);
        }
      }
    }

    reply_num(476, args.at(0) + " :Invalid channel name");
  }

  void client_connection::handlePART(const QList<QByteArray> &args) {
    if (args.isEmpty()) {
      reply_num(461, "PART :Not enough parameters");
      return;
    }

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
      if (!chan_ptr.isNull())
        chan_ptr->part(m_account, message);
      else
        send_raw("442 " + nick + " " + channel_name + " :You're not on that channel");
    }
  }

  // @TODO: support account-tag https://ircv3.net/specs/extensions/account-tag
  // it prefixes the owner of the account who sent this message
  // e.g. @account=Alice :Bob!bob@example.com PRIVMSG #channel :Hello
  // The tag MUST be added by the ircd to all commands sent by a user (e.g. PRIVMSG, MODE, NOTICE, and all others).
  void client_connection::handlePRIVMSG(const QList<QByteArray> &args) {
    if (args.size() < 2) {
      reply_num(461, "PRIVMSG :Not enough parameters");
      return;
    }

    const QByteArray target = args[0];
    const QByteArray text = args[1];

    auto msg = QSharedPointer<QEventMessage>(new QEventMessage);
    msg->account = m_account;
    msg->text = text;
    msg->from_system = false;
    msg->nick = nick;
    msg->raw = args.join(" ");
    msg->user = user;
    msg->host = m_host;

    if (target.startsWith('#')) {
      const auto chan_ptr = Channel::get(target.mid(1));
      if (chan_ptr.isNull()) {
        send_raw("401 " + nick + " " + target + " :No such nick/channel");
        return;
      }

      msg->channel = chan_ptr;
      chan_ptr->message(this, m_account, msg);
    } else {
      if (!g::ctx->irc_nicks.contains(target)) {
        send_raw("401 " + nick + " " + target + " :No such nick/channel");
        return;
      }

      m_account->message(this, g::ctx->irc_nicks[target], msg);
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
    m_socket->disconnectFromHost();
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
    // int users = 0;
    // for (auto *c: m_server->clients())
    //   ++users;
    //
    // replyNumeric(251, nick, "There are " + QByteArray::number(users) + " users and 0 services on 1 servers");
    // replyNumeric(255, nick,
    //   "I have " + QByteArray::number(users) + " clients and " + QByteArray::number(m_server->channels().size())
    //   + " channels");
  }

  void client_connection::change_host(const QByteArray &new_host) {
    m_host = new_host;

    if (!setup_tasks.empty())
      return;

    // @TODO: needs a broadcast to all users in a channel that this m_nick is in
    // https://ircv3.net/specs/extensions/chghost
    // write(":" + prefix() + " CHGHOST " + m_nick + " " + new_host + "\r\n");
    // m_nick = new_host;
  }

  bool client_connection::change_nick(const QByteArray &new_nick) {
    const QByteArray msg = ":" + prefix() + " NICK :" + new_nick + "\r\n";
    emit sendData(msg);
    return true;
  }

  bool client_connection::change_nick(const QSharedPointer<Account> &acc, const QByteArray &old_nick, const QByteArray &new_nick) {
    if (!nick.isEmpty() && new_nick == nick)
      return true;

    const auto _prefix = acc->prefix(0);
    if (_prefix.isEmpty()) {
      qWarning() << "CONN NOT FOUND, could not establish prefix" << acc->name();
      return false;
    }

    const QByteArray msg = ":" + _prefix + " NICK :" + new_nick + "\r\n";
    emit sendData(msg);

    setup_tasks.clear(ConnectionSetupTasks::NICK);
    try_finalize_setup();

    return true;
  }

  void client_connection::send_raw(const QByteArray &line) const {
    const QByteArray out = ":" + ThreadedServer::serverName() + " " + line + "\r\n";
    emit sendData(out);
  }

  void client_connection::change_host(const QSharedPointer<Account> &acc, const QByteArray &new_host) {

  }

  void client_connection::reply_num(const int code, const QByteArray &text) {
    const QByteArray target = nick.isEmpty() ? "*" : nick;
    const QByteArray line = QByteArray::number(code).rightJustified(3, '0') + " " + target + " :" + text;
    send_raw(line);
  }

  QByteArray client_connection::prefix() const {
    return nick + "!" + (user.isEmpty() ? QByteArray("user") : user) + "@" + m_host;
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
    m_account->onConnectionDisconnected(this, nick);
    emit disconnected(nick);
  }

  void client_connection::try_finalize_setup() {
    if (_is_setup || !setup_tasks.empty())
      return;

    const auto server_password = m_server->password();
    if (!server_password.isEmpty()) {
      if (m_passGiven.isEmpty()) {
        reply_num(464, "Password incorrect");
        return m_socket->disconnectFromHost();
      }

      if (m_passGiven != server_password) {
        reply_num(464, "Password incorrect");
        return m_socket->disconnectFromHost();
      }
    }

    reply_num(1, "Hi, welcome to IRC");
    reply_num(2, "Your host is " + ThreadedServer::serverName() + ", running version cIRCa-0.1");
    reply_num(3, "This server was created Dec 21 1989 at 13:37:00 (lie)");
    reply_num(4, ThreadedServer::serverName() + " wut-7.2.2+bla.7.3 what is this.");

    const QByteArray line = "005 " + nick + " BOT=b CASEMAPPING=ascii CHANNELLEN=64 CHANTYPES=# ELIST=U EXCEPTS EXTBAN=,m :are supported by this server";
    send_raw(line);

    handleLUSERS({});
    handleMOTD({});

    if (logged_in)
      handleMODE({nick, "+r"});

    for (const auto& channel : m_account->channels) {
      auto event = QSharedPointer<QEventChannelJoin>(new QEventChannelJoin);
      event->from_system = true;
      event->channel = channel;
      event->account = m_account;
      channel->join(event);
    }

    _is_setup = true;
  }

  void client_connection::handleAUTHENTICATE(const QList<QByteArray> &args) {
    if (args.empty()) {
      send_raw("uwot?");
      m_socket->disconnectFromHost();
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

    const QByteArray& username = plain_spl.at(1);
    const QByteArray& password = plain_spl.at(2);

    const auto account = Account::get_by_name(username);
    if (!account.isNull()) {
      auto auth = QSharedPointer<QEventAuthUser>(new QEventAuthUser);
      auth->username = username;
      auth->password = password;
      auth->ip = m_socket->peerAddress().toString();

      auth = account->verifyPassword(auth);
      if (!auth->cancelled()) {
        account->merge(m_account);
        m_account = account;

        reply_num(900, "You are now logged in as " + username);
        reply_num(903, "SASL authentication successful");
        logged_in = true;
        return;
      }

      QByteArray reply = "SASL authentication failed";
      if (!auth->reason.isEmpty())
        reply += ": " + auth->reason;

      reply_num(900, reply);
      m_socket->disconnectFromHost();
      return;
    }

    reply_num(900, "SASL authentication failed");
    m_socket->disconnectFromHost();
  }

  void client_connection::handleMOTD(const QList<QByteArray> &) const {
    send_raw("375 " + nick + " :- " + ThreadedServer::serverName() + " Message of the day -");
    send_raw("372 " + nick + " :- " + (m_server->motd().isEmpty() ? QByteArray("Welcome!") : m_server->motd()));
    send_raw("376 " + nick + " :End of MOTD command.");
  }

  void client_connection::handleWHO(const QList<QByteArray> &) {
    // note, bot mode has different output: https://ircv3.net/specs/extensions/bot-mode
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
    m_socket->disconnectFromHost();
  }

  void client_connection::parseIncoming(const QByteArray &line) {
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
    else if (cmd == "JOIN") {
      if (parts.at(0) == ":" && parts.size() == 1)
        // replyNumeric(421, m_nick.isEmpty() ? QByteArray("*") : m_nick, "Unknown JOIN command");
          return;
      handleJOIN(parts);
    }
    else if (cmd == "PART")
      handlePART(parts);
    else if (cmd == "PRIVMSG")
      handlePRIVMSG(parts);
    else if (cmd == "QUIT")
      handleQUIT(parts);
    else if (cmd == "NAMES")
      handleNAMES(parts);
    else if (cmd == "TOPIC")
      handleTOPIC(parts);
    else if (cmd == "LUSERS")
      handleLUSERS(parts);
    else if (cmd == "MOTD")
      handleMOTD(parts);
    else if (cmd == "AUTHENTICATE")
      handleAUTHENTICATE(parts);
    else if (cmd == "CAP")
      handleCAP(parts);
    else if (cmd == "MODE")
      handleMODE(parts);
    else {
      return;
      reply_num(421, "Unknown command");
    }

    m_last_activity = QDateTime::currentSecsSinceEpoch();
  }

  client_connection::~client_connection() {
    m_socket->deleteLater();
  }
}