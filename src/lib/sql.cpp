#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "ctx.h"
#include "lib/globals.h"
#include "lib/sql.h"
#include "bcrypt/bcrypt.h"

namespace sql {
  std::unordered_map<unsigned long, QSqlDatabase*> DB_INSTANCES = {};

  // each thread has its own database connection
  QSqlDatabase& ensureThreadDatabase() {
    const unsigned long thread_id = reinterpret_cast<uintptr_t>(QThread::currentThreadId());
    auto it = DB_INSTANCES.find(thread_id);
    if (it != DB_INSTANCES.end()) {
      return *(it->second);
    }

    const QString connection_name = "cr_" + QString::number(thread_id);
    QSqlDatabase* db = new QSqlDatabase(QSqlDatabase::addDatabase("QPSQL", connection_name));
    db->setConnectOptions(QString("application_name=%1").arg(connection_name));
    db->setHostName(g::pgHost);
    db->setPort(g::pgPort);
    db->setDatabaseName(g::pgDatabase);
    db->setUserName(g::pgUsername);
    if (!g::pgPassword.isEmpty())
      db->setPassword(g::pgPassword);

    if (!db->open())
      throw std::runtime_error("failed to open database");

    qDebug() << "db: created connection" << connection_name;

    DB_INSTANCES[thread_id] = db;
    return *db;
  }

  QSharedPointer<QSqlQuery> getQuery() {
    const QSqlDatabase& db = ensureThreadDatabase();
    return QSharedPointer<QSqlQuery>(new QSqlQuery(db));
  }

  QSqlDatabase& getDatabaseForThread() {
    return ensureThreadDatabase();
  }

  QSharedPointer<QSqlQuery> exec(const QString &sql) {
    auto q = getQuery();
    const auto res = q->exec(sql);
    if (!res) {
      const auto err = q->lastError().text();
      if (!err.contains("already exists"))
        qCritical() << "SQL error: " << err;
    }
    return q;
  }

  QSharedPointer<QSqlQuery> exec(const QSharedPointer<QSqlQuery> &q) {
    if (const auto res = q->exec(); !res) {
      if (const auto err = q->lastError().text(); !err.contains("already exists"))
        qCritical() << "SQL error: " << err;
    }
    return q;
  }

  void create_schema() {
    exec(R"(
    CREATE TABLE IF NOT EXISTS accounts (
      id UUID PRIMARY KEY,
      username TEXT UNIQUE NOT NULL,
      password TEXT NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS servers (
      id UUID PRIMARY KEY,
      name TEXT NOT NULL,
      account_owner_id UUID NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS server_members (
      account_id UUID NOT NULL,
      server_id UUID NOT NULL,
      join_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(account_id, server_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
      FOREIGN KEY(server_id) REFERENCES servers(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS channels (
      id UUID PRIMARY KEY,
      server_id UUID NOT NULL,
      name TEXT NOT NULL,
      topic TEXT,
      account_owner_id UUID,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(server_id) REFERENCES servers(id) ON DELETE CASCADE,
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id) ON DELETE SET NULL,
      UNIQUE(server_id, name)
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS uploads (
      id UUID PRIMARY KEY,
      account_owner_id UUID NOT NULL,
      path TEXT NOT NULL,
      type INTEGER NOT NULL,
      variant INTEGER NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS roles (
      id UUID PRIMARY KEY,
      server_id UUID NOT NULL,
      name TEXT NOT NULL,
      icon UUID,
      color INTEGER,
      priority INTEGER DEFAULT 0,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(server_id) REFERENCES servers(id) ON DELETE CASCADE,
      FOREIGN KEY(icon) REFERENCES uploads(id) ON DELETE SET NULL,
      UNIQUE(server_id, name)
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS account_channels (
      account_id UUID NOT NULL,
      channel_id UUID NOT NULL,
      PRIMARY KEY(account_id, channel_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
      FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS account_roles (
      account_id UUID NOT NULL,
      role_id UUID NOT NULL,
      PRIMARY KEY(account_id, role_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
      FOREIGN KEY(role_id) REFERENCES roles(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS permissions (
      id UUID PRIMARY KEY,
      role_id UUID NOT NULL,
      permission_bits INTEGER NOT NULL DEFAULT 0,
      FOREIGN KEY(role_id) REFERENCES roles(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS logins (
      id UUID PRIMARY KEY,
      account_id UUID NOT NULL,
      ip TEXT NOT NULL,
      login_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS events (
      id UUID PRIMARY KEY,
      account_id UUID NOT NULL,
      channel_id UUID,
      recipient_id UUID,
      event_type INTEGER NOT NULL,
      data TEXT NOT NULL,
      reply_to UUID,
      display_name TEXT,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
      FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE SET NULL,
      FOREIGN KEY(recipient_id) REFERENCES accounts(id) ON DELETE SET NULL,
      FOREIGN KEY(reply_to) REFERENCES events(id) ON DELETE SET NULL
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS messages (
      id UUID PRIMARY KEY,
      sender_id UUID NOT NULL,
      channel_id UUID,
      text TEXT NOT NULL,
      raw BYTEA,
      tags TEXT,
      nick TEXT,
      host TEXT,
      username TEXT,
      targets TEXT,
      from_system INTEGER DEFAULT 0,
      tag_msg INTEGER DEFAULT 0,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(sender_id) REFERENCES accounts(id) ON DELETE CASCADE,
      FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE SET NULL
    )
    )");

    // enum: metadata:ref_type
    exec(R"(
    DO $$
    BEGIN
      IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'ref_type_enum') THEN
        CREATE TYPE ref_type_enum AS ENUM ('channel', 'account');
      END IF;
    END$$;
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS metadata (
      id UUID PRIMARY KEY,
      key TEXT NOT NULL,
      value BYTEA NOT NULL,
      ref_id UUID NOT NULL,
      ref_type ref_type_enum NOT NULL,
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      CONSTRAINT metadata_ref_key_unique UNIQUE (ref_id, key)
    )
    )");

    exec(R"(
    CREATE TABLE IF NOT EXISTS metadata_subs (
      id UUID PRIMARY KEY,
      metadata_id UUID NOT NULL,
      account_id UUID NOT NULL,
      FOREIGN KEY(metadata_id) REFERENCES metadata(id) ON DELETE CASCADE,
      FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
    )
    )");

    // Indices for metadata table
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_ref ON metadata(ref_id, ref_type)");
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_ref_key ON metadata(ref_id, ref_type, key)");
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_created_at ON metadata(created_at DESC)");
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_modified_at ON metadata(modified_at DESC)");

    // Indices for metadata_subs table
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_subs_metadata ON metadata_subs(metadata_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_subs_account ON metadata_subs(account_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_metadata_subs_metadata_account ON metadata_subs(metadata_id, account_id)");

    // Optional indexes for filtering and performance
    exec("CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_messages_channel ON messages(channel_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_messages_date ON messages(creation_date DESC)");

    // Index for event_type
    exec("CREATE INDEX IF NOT EXISTS idx_channels_account_owner ON channels(account_owner_id)");

    // Index for event_type
    exec("CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type)");

    // Index for filtering events by sender
    exec("CREATE INDEX IF NOT EXISTS idx_events_sender ON events(account_id)");

    // Index for filtering events by recipient (1:1 events)
    exec("CREATE INDEX IF NOT EXISTS idx_events_recipient ON events(recipient_id)");

    // Index for filtering events by channel
    exec("CREATE INDEX IF NOT EXISTS idx_events_channel ON events(channel_id)");

    // Index for sorting all events by creation_date
    exec("CREATE INDEX IF NOT EXISTS idx_events_date ON events(creation_date DESC)");

    // Composite index: sender + creation_date
    exec("CREATE INDEX IF NOT EXISTS idx_events_sender_date ON events(account_id, creation_date DESC)");

    // Composite index: recipient + creation_date
    exec("CREATE INDEX IF NOT EXISTS idx_events_recipient_date ON events(recipient_id, creation_date DESC)");

    // Composite index: channel + creation_date
    exec("CREATE INDEX IF NOT EXISTS idx_events_channel_date ON events(channel_id, creation_date DESC)");

    // Index for reply_to
    exec("CREATE INDEX IF NOT EXISTS idx_events_reply ON events(reply_to)");

    // Index for server membership lookups
    exec("CREATE INDEX IF NOT EXISTS idx_server_members_account ON server_members(account_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_server_members_server ON server_members(server_id)");

    // Index for roles lookup by server
    exec("CREATE INDEX IF NOT EXISTS idx_roles_server ON roles(server_id)");

    // Index for account_roles lookup by role
    exec("CREATE INDEX IF NOT EXISTS idx_account_roles_role ON account_roles(role_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_account_roles_account ON account_roles(account_id)");

    // Index for permissions lookup by role
    exec("CREATE INDEX IF NOT EXISTS idx_permissions_role ON permissions(role_id)");

    // Index for channels by server for quick filtering
    exec("CREATE INDEX IF NOT EXISTS idx_channels_server ON channels(server_id)");

    // Index for uploads by account_owner
    exec("CREATE INDEX IF NOT EXISTS idx_uploads_account_owner ON uploads(account_owner_id)");
  }

  QUuid metadata_create(const QByteArray& key, const QByteArray& value, const QUuid ref_id, RefType ref_type) {
    const auto q = getQuery();

    q->prepare(R"(
      INSERT INTO metadata (
        id, key, value, ref_id, ref_type
      ) VALUES (?, ?, ?, ?, ?)
    )");

    const auto uuid = QUuid::createUuid();

    q->addBindValue(uuid);                     // id
    q->addBindValue(QString::fromUtf8(key));   // key
    q->addBindValue(value);                    // value
    q->addBindValue(ref_id);                   // ref_id

    // enum
    QString ref_type_str;
    switch(ref_type) {
      case RefType::Channel: ref_type_str = "channel"; break;
      case RefType::Account: ref_type_str = "account"; break;
    }
    q->addBindValue(ref_type_str);       // ref_type

    if (!q->exec()) {
      qWarning() << "Failed to insert metadata:" << q->lastError().text();
      return {};
    }

    return uuid;
  }

  MetadataResult metadata_get(const QUuid ref_id) {
    MetadataResult result;
    const auto q = getQuery();

    q->prepare(R"(
      SELECT m.key, m.value, ms.account_id
      FROM metadata m
      LEFT JOIN metadata_subs ms ON ms.metadata_id = m.id
      WHERE m.ref_id = ?
    )");

    q->addBindValue(ref_id);

    if (!q->exec()) {
      qWarning() << "failed to fetch metadata:" << q->lastError().text();
      return result;
    }

    while (q->next()) {
      const QString key = q->value("key").toString();
      const QVariant value = q->value("value");

      // set key/value
      result.keyValues.insert(key, value);

      // handle subscribers
      const auto account_id = q->value("account_id").toUuid();
      if (account_id.isNull())
        continue;

      auto account_ptr = Ctx::instance()->accounts_lookup_uuid.value(account_id);
      if (!account_ptr)
        continue;

      result.subscribers[key].insert(account_ptr);
    }

    return result;
  }

  bool metadata_remove(const QByteArray& key, QUuid ref_id) {
    const auto q = getQuery();

    q->prepare(R"(
      DELETE FROM metadata
      WHERE ref_id = ? AND key = ?
    )");

    q->addBindValue(ref_id);
    q->addBindValue(key);

    if (!q->exec()) {
      qWarning() << "Failed to remove metadata:" << q->lastError().text();
      return false;
    }

    return true;
  }

  bool metadata_modify(QUuid ref_id, const QByteArray& key, const QByteArray& new_value) {
    const auto q = getQuery();

    q->prepare(R"(
    UPDATE metadata
    SET value = ?, modified_at = CURRENT_TIMESTAMP
    WHERE ref_id = ? AND key = ?
  )");

    q->addBindValue(new_value);
    q->addBindValue(ref_id);
    q->addBindValue(QString::fromUtf8(key));

    if (!q->exec()) {
      qWarning() << "Failed to modify metadata:" << q->lastError().text();
      return false;
    }

    return true;
  }

  bool metadata_upsert(const QByteArray& key, const QByteArray& value, QUuid ref_id, RefType ref_type) {
    const auto q = getQuery();

    q->prepare(R"(
      INSERT INTO metadata (id, key, value, ref_id, ref_type)
      VALUES (?, ?, ?, ?, ?)
      ON CONFLICT (ref_id, key)
      DO UPDATE SET
        value = EXCLUDED.value,
        modified_at = CURRENT_TIMESTAMP
    )");

    const auto uuid = QUuid::createUuid();

    q->addBindValue(uuid);                          // id
    q->addBindValue(QString::fromUtf8(key));        // key
    q->addBindValue(value);                         // value
    q->addBindValue(ref_id);                        // ref_id

    QString ref_type_str;
    switch (ref_type) {
      case RefType::Channel: ref_type_str = "channel"; break;
      case RefType::Account: ref_type_str = "account"; break;
    }
    q->addBindValue(ref_type_str);                  // ref_type

    if (!q->exec()) {
      qWarning() << "Failed to upsert metadata:" << q->lastError().text();
      return false;
    }

    return true;
  }

  bool metadata_subscribe(QUuid ref_id, const QByteArray& key, QUuid account_id) {
    const auto q1 = getQuery();
      q1->prepare(R"(
      SELECT id FROM metadata
      WHERE ref_id = ? AND key = ?
    )");
    q1->addBindValue(ref_id);
    q1->addBindValue(QString::fromUtf8(key));

    if (!q1->exec() || !q1->next()) {
      qWarning() << "Failed to find metadata for subscription:" << q1->lastError().text();
      return false;
    }

    const QUuid metadata_id = q1->value("id").toUuid();

    const auto q2 = getQuery();
    q2->prepare(R"(
      INSERT INTO metadata_subs (id, metadata_id, account_id)
      VALUES (?, ?, ?)
      ON CONFLICT (metadata_id, account_id) DO NOTHING
    )");

    q2->addBindValue(QUuid::createUuid());
    q2->addBindValue(metadata_id);
    q2->addBindValue(account_id);

    if (!q2->exec()) {
      qWarning() << "Failed to subscribe account to metadata:" << q2->lastError().text();
      return false;
    }

    return true;
  }

  bool metadata_subscribe_bulk(QUuid ref_id, const QList<QByteArray>& keys, QUuid account_id) {
    if (keys.isEmpty())
      return false;

    constexpr int INSERT_BATCH_SIZE = 500;

    QSqlDatabase& db = ensureThreadDatabase();
    db.transaction();

    // resolve metadata IDs for all keys
    QMap<QByteArray, QUuid> keyToMetadataId;
    {
      QStringList keyStrings;
      for (const auto& k : keys)
        keyStrings << QString::fromUtf8(k);

      QStringList placeholders;
      for (int i = 0; i < keyStrings.size(); ++i)
        placeholders << "?";

      const QString queryStr = QString(R"(
        SELECT id, key FROM metadata
        WHERE ref_id = ? AND key IN (%1)
      )").arg(placeholders.join(", "));

      auto q = getQuery();
      q->prepare(queryStr);

      q->addBindValue(ref_id);

      for (const auto& key : keyStrings)
        q->addBindValue(key);

      if (!q->exec()) {
        qWarning() << "Failed to resolve metadata IDs for bulk subscribe:" << q->lastError().text();
        db.rollback();
        return false;
      }

      while (q->next()) {
        const auto key = q->value("key").toString().toUtf8();
        const auto id = q->value("id").toUuid();
        keyToMetadataId.insert(key, id);
      }
    }

    if (keyToMetadataId.isEmpty()) {
      qWarning() << "No metadata found for given keys in metadata_subscribe_bulk";
      db.rollback();
      return false;
    }

    // prepare bulk insert
    const auto q = getQuery();
    q->prepare(R"(
      INSERT INTO metadata_subs (id, metadata_id, account_id)
      VALUES (?, ?, ?)
      ON CONFLICT (metadata_id, account_id) DO NOTHING
    )");

    QList<QVariant> ids, metadata_ids, account_ids;
    int count = 0;
    bool ok = true;

    auto flush = [&]() {
      if (ids.isEmpty())
        return;

      q->addBindValue(ids);
      q->addBindValue(metadata_ids);
      q->addBindValue(account_ids);

      if (!q->execBatch()) {
        qWarning() << "Failed to bulk insert metadata subscriptions:" << q->lastError().text();
        ok = false;
        return;
      }

      ids.clear();
      metadata_ids.clear();
      account_ids.clear();
    };

    for (const auto& k : keys) {
      auto metadata_id = keyToMetadataId.value(k);
      if (metadata_id.isNull())
        continue;

      ids << QUuid::createUuid();
      metadata_ids << metadata_id;
      account_ids << account_id;

      count++;
      if (count % INSERT_BATCH_SIZE == 0) {
        flush();
        if (!ok) {
          db.rollback();
          return false;
        }
      }
    }

    flush();
    if (!ok) {
      db.rollback();
      return false;
    }

    db.commit();
    return true;
  }

  bool metadata_unsubscribe_bulk(QUuid ref_id, const QList<QByteArray>& keys, QUuid account_id) {
    if (keys.isEmpty())
      return false;

    QSqlDatabase db = ensureThreadDatabase();
    db.transaction();

    // resolve metadata IDs for the given keys
    QMap<QByteArray, QUuid> keyToMetadataId;
    {
      const auto q = getQuery();
      QStringList keyStrings;
      for (const auto& k : keys)
        keyStrings << QString::fromUtf8(k);

      QStringList placeholders;
      for (int i = 0; i < keyStrings.size(); ++i)
        placeholders << "?";

      QString queryStr = QString(R"(
        SELECT id, key FROM metadata
        WHERE ref_id = ? AND key IN (%1)
      )").arg(placeholders.join(", "));

      q->prepare(queryStr);
      q->addBindValue(ref_id);

      for (const auto& key : keyStrings)
        q->addBindValue(key);

      if (!q->exec()) {
        qWarning() << "Failed to resolve metadata IDs for bulk unsubscribe:" << q->lastError().text();
        db.rollback();
        return false;
      }

      while (q->next()) {
        const auto key = q->value("key").toString().toUtf8();
        const auto id = q->value("id").toUuid();
        keyToMetadataId.insert(key, id);
      }
    }

    if (keyToMetadataId.isEmpty()) {
      qWarning() << "No metadata found for given keys in metadata_unsubscribe_bulk";
      db.rollback();
      return false;
    }

    // delete subscriptions for the resolved metadata IDs
    const auto q = getQuery();
    QList<QUuid> metadataIds = keyToMetadataId.values();
    QStringList placeholders;
    for (int i = 0; i < metadataIds.size(); ++i)
      placeholders << "?";

    const QString queryStr = QString(R"(
      DELETE FROM metadata_subs
      WHERE account_id = ? AND metadata_id IN (%1)
    )").arg(placeholders.join(", "));

    q->prepare(queryStr);
    q->addBindValue(account_id);

    for (const auto& id : metadataIds)
      q->addBindValue(id);

    if (!q->exec()) {
      qWarning() << "Failed to bulk unsubscribe metadata:" << q->lastError().text();
      db.rollback();
      return false;
    }

    db.commit();
    return true;
  }

  bool metadata_unsubscribe(QUuid ref_id, const QByteArray& key, QUuid account_id) {
    const auto q1 = getQuery();
    q1->prepare(R"(
      SELECT id FROM metadata
      WHERE ref_id = ? AND key = ?
    )");
    q1->addBindValue(ref_id);
    q1->addBindValue(QString::fromUtf8(key));

    if (!q1->exec() || !q1->next()) {
      qWarning() << "Failed to find metadata for unsubscription:" << q1->lastError().text();
      return false;
    }

    const QUuid metadata_id = q1->value("id").toUuid();

    const auto q2 = getQuery();
      q2->prepare(R"(
      DELETE FROM metadata_subs
      WHERE metadata_id = ? AND account_id = ?
    )");
    q2->addBindValue(metadata_id);
    q2->addBindValue(account_id);

    if (!q2->exec()) {
      qWarning() << "Failed to unsubscribe account from metadata:" << q2->lastError().text();
      return false;
    }

    return true;
  }

  QUuid insert_message(const QSharedPointer<QEventMessage>& msg) {
    if (!msg)
      return {};

    const auto q = getQuery();

    q->prepare(R"(
      INSERT INTO messages (
        id, sender_id, channel_id, text, raw, tags,
        nick, host, username, targets, from_system, tag_msg
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    const auto uuid = QUuid::createUuid();

    q->addBindValue(uuid);                                             // id
    q->addBindValue(msg->account ? msg->account->uid() : QUuid());     // sender_id
    q->addBindValue(msg->channel ? msg->channel->uid : QUuid());       // channel_id
    q->addBindValue(msg->text);                                        // text
    q->addBindValue(msg->raw);                                         // raw
    q->addBindValue(QJsonDocument::fromVariant(msg->tags).toJson(QJsonDocument::Compact)); // tags
    q->addBindValue(msg->nick);                                        // nick
    q->addBindValue(msg->host);                                        // host
    q->addBindValue(msg->user);                                        // username
    q->addBindValue(msg->targets.join(","));                           // targets
    q->addBindValue(msg->from_system ? 1 : 0);                         // from_system
    q->addBindValue(msg->tag_msg ? 1 : 0);                             // tag_msg

    if (!q->exec()) {
      qWarning() << "Failed to insert message:" << q->lastError().text();
      return {};
    }

    return uuid;
  }

  void insert_messages(const QSet<QSharedPointer<QEventMessage>>& messages) {
    if (messages.isEmpty())
      return;

    constexpr int INSERT_BATCH_SIZE = 1000;

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    const auto q = getQuery();
    q->prepare(R"(
      INSERT INTO messages (
        id, account_id, channel_id, text, raw, tags,
        nick, host, username, targets, from_system, tag_msg
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    QList<QVariant> ids, account_ids, channel_ids, texts, raws, tagss, nicks, hosts, users, targetss, from_systems, tag_msgs;
    int count = 0;

    auto flush = [&] {
      if (ids.isEmpty())
        return;

      q->addBindValue(ids);
      q->addBindValue(account_ids);
      q->addBindValue(channel_ids);
      q->addBindValue(texts);
      q->addBindValue(raws);
      q->addBindValue(tagss);
      q->addBindValue(nicks);
      q->addBindValue(hosts);
      q->addBindValue(users);
      q->addBindValue(targetss);
      q->addBindValue(from_systems);
      q->addBindValue(tag_msgs);

      q->execBatch();

      ids.clear(); account_ids.clear(); channel_ids.clear();
      texts.clear(); raws.clear(); tagss.clear();
      nicks.clear(); hosts.clear(); users.clear();
      targetss.clear(); from_systems.clear(); tag_msgs.clear();
    };

    for (const auto& msg : messages) {
      ids << QUuid::createUuid();

      account_ids << (msg->account ? msg->account->uid() : QUuid());
      channel_ids << (msg->channel ? msg->channel->uid : QUuid());
      texts << msg->text;
      raws << msg->raw;
      tagss << QJsonDocument::fromVariant(msg->tags).toJson(QJsonDocument::Compact);
      nicks << msg->nick;
      hosts << msg->host;
      users << msg->user;
      targetss << msg->targets.join(",");
      from_systems << (msg->from_system ? 1 : 0);
      tag_msgs << (msg->tag_msg ? 1 : 0);

      count++;
      if (count % INSERT_BATCH_SIZE == 0) {
        flush();
      }
    }

    flush(); // insert remaining messages
    db.commit();
  }

  QSharedPointer<Account> account_get_or_create(const QByteArray &username, const QByteArray &password) {
    auto it = g::ctx->accounts_lookup_name.find(username);
    if (it != g::ctx->accounts_lookup_name.end() && !it.value().isNull()) {
      return it.value();
    }

    {
      const auto q = getQuery();
      q->prepare("SELECT id, username, password, creation_date FROM accounts WHERE username = ?");
      q->addBindValue(QString::fromUtf8(username));

      if (!q->exec()) {
        qWarning() << "Query failed:" << q->lastError().text();
        return nullptr;
      }

      if (q->next()) {
        return Account::create_from_db(
          q->value("id").toUuid(),
          q->value("username").toByteArray(),
          q->value("password").toByteArray(),
          q->value("creation_date").toDateTime()
          );
      }
    }

    // create new account
    const auto uuid = QUuid::createUuid();
    const QString hashedPassword = hashPasswordBcrypt(QString::fromUtf8(password));

    const auto q_insert = getQuery();
    q_insert->prepare("INSERT INTO accounts (id, username, password) VALUES (?, ?, ?)");
    q_insert->addBindValue(uuid.toString());
    q_insert->addBindValue(QString::fromUtf8(username));
    q_insert->addBindValue(hashedPassword);

    if (!q_insert->exec()) {
      qCritical() << "account_get_or_create insert error:" << q_insert->lastError().text();
      return nullptr;
    }

    return Account::create_from_db(uuid, username, hashedPassword.toUtf8(), QDateTime::currentDateTime());
  }

  QSharedPointer<Upload> upload_get_or_create(
      const QUuid &accountId,
      const QString &path,
      int type,
      int variant) {

    const auto q = getQuery();
    q->prepare(
      "SELECT id, account_owner_id, path, type, variant, creation_date FROM uploads WHERE account_owner_id = ? AND path = ?");
    q->addBindValue(accountId);
    q->addBindValue(path);

    if (!q->exec()) {
      qCritical() << "upload_get_or_create select error:" << q->lastError().text();
      return nullptr;
    }

    if (q->next()) {
      return Upload::create_from_db(
        q->value("id").toUuid(),
        q->value("account_owner_id").toUuid(),
        q->value("path").toString(),
        q->value("type").toInt(),
        q->value("variant").toInt(),
        q->value("creation_date").toDateTime()
        );
    }

    const auto uuid = QUuid::createUuid();
    const auto q_insert = getQuery();
    q_insert->prepare("INSERT INTO uploads (id, account_owner_id, path, type, variant) VALUES (?, ?, ?, ?, ?)");
    q_insert->addBindValue(uuid);
    q_insert->addBindValue(accountId);
    q_insert->addBindValue(path);
    q_insert->addBindValue(type);
    q_insert->addBindValue(variant);

    if (!q_insert->exec()) {
      qCritical() << "upload_get_or_create insert error:" << q_insert->lastError().text();
      return nullptr;
    }

    return Upload::create_from_db(uuid, accountId, path, type, variant, QDateTime::currentDateTime());
  }

  QSharedPointer<Permission> permission_get_or_create(const QUuid &roleId, Permission::PermissionFlags flags) {
    const auto q = getQuery();
    q->prepare("SELECT id, role_id, permission_bits FROM permissions WHERE role_id = ?");
    q->addBindValue(roleId);

    if (!q->exec()) {
      qCritical() << "permission_get_or_create select error:" << q->lastError().text();
      return nullptr;
    }

    if (q->next()) {
      return Permission::create_from_db(
        q->value("id").toUuid(),
        q->value("role_id").toUuid(),
        q->value("permission_bits").toInt()
        );
    }

    const auto uuid = QUuid::createUuid();
    const auto insertQuery = getQuery();
    insertQuery->prepare("INSERT INTO permissions (id, role_id, permission_bits) VALUES (?, ?, ?)");
    insertQuery->addBindValue(uuid);
    insertQuery->addBindValue(roleId);
    insertQuery->addBindValue(flags.bits);

    if (!insertQuery->exec()) {
      qCritical() << "permission_get_or_create insert error:" << insertQuery->lastError().text();
      return nullptr;
    }

    return Permission::create_from_db(uuid, roleId, flags.bits);
  }

  QSharedPointer<Role> create_role_for_server(
      const QSharedPointer<Server> &server,
      const QString &roleName,
      const int priority,
      const QUuid &iconId,
      const bool assignToExistingMembers,
      const Permission::PermissionFlags defaultPermissions) {
    if (!server || server->uid().isNull())
      return nullptr;

    // -------------------------
    // Check if role already exists
    // -------------------------
    const auto q_select = getQuery();
    q_select->prepare(
      "SELECT id, server_id, name, icon, color, priority, creation_date "
      "FROM roles WHERE server_id = ? AND name = ?"
    );
    q_select->addBindValue(server->uid());
    q_select->addBindValue(roleName);

    if (!q_select->exec()) {
      qCritical() << "create_role_for_server select error:" << q_select->lastError().text();
      return nullptr;
    }

    if (q_select->next()) {
      // Role already exists, add to server cache if not present
      const QUuid existingRoleId = q_select->value("id").toUuid();
      auto role = Role::create_from_db(
        existingRoleId,
        q_select->value("server_id").toUuid(),
        q_select->value("name").toByteArray(),
        q_select->value("icon").toUuid(),
        q_select->value("color").toInt(),
        q_select->value("priority").toInt(),
        q_select->value("creation_date").toDateTime()
      );

      server->add_role(role); // ensure role is cached
      return role;
    }

    // -------------------------
    // create new role
    // -------------------------
    const auto role_uid = QUuid::createUuid();
    const auto q_insert = getQuery();
    q_insert->prepare(
      "INSERT INTO roles (id, server_id, name, priority, icon) VALUES (?, ?, ?, ?, ?)"
    );
    q_insert->addBindValue(role_uid);        // UUID
    q_insert->addBindValue(server->uid());   // UUID
    q_insert->addBindValue(roleName);        // TEXT
    q_insert->addBindValue(priority);        // INTEGER
    if (iconId.isNull()) {
      q_insert->addBindValue(QVariant(QMetaType(QMetaType::QUuid))); // NULL BLOB
    } else {
      q_insert->addBindValue(iconId); // UUID
    }

    if (!q_insert->exec()) {
      qCritical() << "create_role_for_server insert error:" << q_insert->lastError().text();
      return nullptr;
    }

    // -------------------------
    // Fetch the newly created role to get creation_date
    // -------------------------
    const auto q_fetch = getQuery();
    q_fetch->prepare(
      "SELECT id, server_id, name, icon, color, priority, creation_date FROM roles WHERE id = ?"
    );
    q_fetch->addBindValue(role_uid);

    if (!q_fetch->exec() || !q_fetch->next()) {
      qCritical() << "Failed to fetch newly created role:" << q_fetch->lastError().text();
      return nullptr;
    }

    auto role = Role::create_from_db(
      q_fetch->value("id").toUuid(),
      q_fetch->value("server_id").toUuid(),
      q_fetch->value("name").toByteArray(),
      q_fetch->value("icon").toUuid(),
      q_fetch->value("color").toInt(),
      q_fetch->value("priority").toInt(),
      q_fetch->value("creation_date").toDateTime()
    );

    // Cache role in server
    server->add_role(role);

    // -------------------------
    // Assign default permissions
    // -------------------------
    auto perm = permission_get_or_create(role->uid(), defaultPermissions);
    if (!perm) {
      qWarning() << "Failed to create default permissions for role" << roleName;
    }

    // -------------------------
    // Optionally assign role to all existing members
    // -------------------------
    if (assignToExistingMembers) {
      for (const auto &member : server->all_accounts()) {
        const auto q_ar = getQuery();
        q_ar->prepare("INSERT INTO account_roles (account_id, role_id) VALUES (?, ?) ON CONFLICT DO NOTHING");
        q_ar->addBindValue(member->uid());
        q_ar->addBindValue(role_uid);
        if (!q_ar->exec()) {
          qWarning() << "Failed to assign role" << roleName
                     << "to member" << member->name() << ":" << q_ar->lastError().text();
        }
      }
    }

    return role;
  }

  bool assign_role_to_account(
      const QUuid &accountId,
      const QSharedPointer<Role> &role) {
    if (!role || accountId.isNull())
      return false;

    const auto q = getQuery();
    q->prepare("INSERT INTO account_roles (account_id, role_id) VALUES (?, ?) ON CONFLICT DO NOTHING");
    q->addBindValue(accountId);
    q->addBindValue(role->uid());

    if (!q->exec()) {
      qWarning() << "Failed to assign role" << role->name()
                 << "to account" << accountId
                 << ":" << q->lastError().text();
      return false;
    }

    return true;
  }

  bool server_add_member(const QUuid &accountId, const QUuid &serverId) {
    if (accountId.isNull() || serverId.isNull()) {
      qCritical() << "server_add_member called with empty accountId or serverId";
      return false;
    }

    // prevent duplicate membership
    const auto q_check = getQuery();
    q_check->prepare("SELECT 1 FROM server_members WHERE account_id = ? AND server_id = ?");
    q_check->addBindValue(accountId);
    q_check->addBindValue(serverId);
    if (!q_check->exec()) {
      qCritical() << "server_add_member check error:" << q_check->lastError().text();
      return false;
    }
    if (q_check->next()) {
      // already a member
      return true;
    }

    // insert into server_members table
    const auto q = getQuery();
    q->prepare("INSERT INTO server_members (account_id, server_id) VALUES (?, ?)");
    q->addBindValue(accountId);
    q->addBindValue(serverId);
    if (!q->exec()) {
      qCritical() << "server_add_member insert error:" << q->lastError().text();
      return false;
    }

    // update in-memory cache
    const auto server = Server::get_by_uid(serverId);
    if (!server.isNull()) {
      QReadLocker locker(&g::ctx->mtx_cache);

      // add account to server cache
      const auto account = Account::get_by_uid(accountId);
      if (!account.isNull()) {
        server->add_account(account);
      }

      if (const auto everyone_role = server->role_by_name("@everyone"); !everyone_role.isNull()) {
        assign_role_to_account(accountId, everyone_role);
      } else {
        qWarning() << "No @everyone role found for server" << server->uid();
      }
    }

    return true;
  }

  QSharedPointer<Server> server_get_or_create(const QByteArray &name, const QSharedPointer<Account> &owner) {
    if (owner.isNull()) {
      qCritical() << "server_get_or_create requires a non-null owner";
      return nullptr;
    }

    // check cache first
    const auto it = g::ctx->servers_lookup_name.find(name);
    if (it != g::ctx->servers_lookup_name.end() && !it.value().isNull())
      return it.value();

    QSharedPointer<Server> server;

    // --- Check DB ---
    const auto q = getQuery();
    q->prepare("SELECT id, name, account_owner_id, creation_date FROM servers WHERE name = ?");
    q->addBindValue(name);

    if (!q->exec()) {
      qCritical() << "server_get_or_create select error:" << q->lastError().text();
      return nullptr;
    }

    if (q->next()) {
      const QUuid ownerId = q->value("account_owner_id").toUuid();
      const QSharedPointer<Account> ownerPtr = Account::get_by_uid(ownerId);

      server = Server::create_from_db(
        q->value("id").toUuid(),
        q->value("name").toByteArray(),
        ownerPtr,
        q->value("creation_date").toDateTime()
      );

    } else {
      // --- Create server in DB ---
      const auto uuid = QUuid::createUuid();
      const auto q_insert = getQuery();
      q_insert->prepare("INSERT INTO servers (id, name, account_owner_id) VALUES (?, ?, ?)");
      q_insert->addBindValue(uuid);
      q_insert->addBindValue(name);
      q_insert->addBindValue(owner->uid());

      if (!q_insert->exec()) {
        qCritical() << "server_get_or_create insert error:" << q_insert->lastError().text();
        return nullptr;
      }

      server = Server::create_from_db(uuid, name, owner, QDateTime::currentDateTime());
    }

    // --- Ensure @everyone role exists immediately ---
    auto everyoneRole = create_role_for_server(server, "@everyone", 0, {}, true);
    if (!everyoneRole) {
      qCritical() << "Failed to create @everyone role for server" << server->uid();
    }

    // --- Add owner as a member ---
    server_add_member(owner->uid(), server->uid());
    server->add_account(owner);

    // --- Load existing members from DB ---
    const auto q_members = getQuery();
    q_members->prepare("SELECT account_id FROM server_members WHERE server_id = ?");
    q_members->addBindValue(server->uid());
    if (q_members->exec()) {
      QReadLocker locker(&g::ctx->mtx_cache);
      while (q_members->next()) {
        const QUuid acc_id = q_members->value("account_id").toUuid();
        if (g::ctx->accounts_lookup_uuid.contains(acc_id))
          server->add_account(g::ctx->accounts_lookup_uuid[acc_id]);
      }
    }

    return server;
  }

  bool account_exists(const QByteArray &username) {
    if (g::ctx->accounts_lookup_name.contains(username))
      return true;

    const auto q = getQuery();
    q->prepare("SELECT 1 FROM accounts WHERE username = ? LIMIT 1");
    q->addBindValue(username);

    if (!q->exec()) {
      qCritical() << "accountExists query error:" << q->lastError().text();
      return false;
    }

    return q->next();
  }

  QList<QSharedPointer<Channel>> account_get_channels(const QUuid &account_id) {
    QList<QSharedPointer<Channel>> channels;

    const auto q = getQuery();
    q->prepare(R"(
      SELECT c.id, c.name, c.topic, c.account_owner_id, c.server_id, c.creation_date
      FROM channels c
      INNER JOIN account_channels ac ON c.id = ac.channel_id
      WHERE ac.account_id = ?
    )");
    q->addBindValue(account_id);

    if (!q->exec()) {
      qCritical() << "account_get_channels query error:" << q->lastError().text();
      return channels;
    }

    while (q->next()) {
      QSharedPointer<Account> acc;
      if (const auto acc_uid = q->value("account_owner_id").toUuid(); !acc_uid.isNull())
        acc = Account::get_by_uid(acc_uid);

      QSharedPointer<Server> server;
      if (const auto server_uid = q->value("server_id").toUuid(); !server_uid.isNull())
        server = Server::get_by_uid(server_uid);

      channels.append(Channel::create_from_db(
        q->value("id").toUuid(),
        q->value("name").toByteArray(),
        q->value("topic").toByteArray(),
        acc,
        server,
        q->value("creation_date").toDateTime()
        ));
    }

    return channels;
  }

  QList<QSharedPointer<Account>> account_get_all() {
    QList<QSharedPointer<Account>> accounts;
    const int batch_size = 100;
    int offset = 0;

    while (true) {
      const auto q = getQuery();
      q->prepare("SELECT id, username, password, creation_date FROM accounts LIMIT ? OFFSET ?");
      q->addBindValue(batch_size);
      q->addBindValue(offset);

      if (!q->exec()) {
        qCritical() << "getAllAccounts query error:" << q->lastError().text();
        break;
      }

      int rowCount = 0;
      while (q->next()) {
        accounts.append(Account::create_from_db(
          q->value("id").toUuid(),
          q->value("username").toByteArray(),
          q->value("password").toByteArray(),
          q->value("creation_date").toDateTime()
          ));
        rowCount++;
      }

      if (rowCount < batch_size)
        break;
      offset += batch_size;
    }

    return accounts;
  }

  bool channel_exists(const QByteArray &name) {
    if (g::ctx->channels.contains(name))
      return true;

    const auto q = getQuery();
    q->prepare("SELECT 1 FROM channels WHERE name = ? LIMIT 1");
    q->addBindValue(name);

    if (!q->exec()) {
      qCritical() << "channel_exists query error:" << q->lastError().text();
      return false;
    }

    return q->next();
  }

  QSharedPointer<Channel> channel_get_or_create(
      const QByteArray &name,
      const QByteArray &topic,
      const QSharedPointer<Account> &owner,
      const QSharedPointer<Server> &server) {
    // check in-memory cache first
    if (const auto it = g::ctx->channels.find(name); it != g::ctx->channels.end() && !it.value().isNull())
      return it.value();

    const auto q = getQuery();
    q->prepare(R"(
      SELECT id, name, topic, account_owner_id, server_id, creation_date
      FROM channels
      WHERE name = ?
    )");
    q->addBindValue(name);

    if (!q->exec()) {
      qCritical() << "channel_get_or_create select error:" << q->lastError().text();
      return nullptr;
    }

    // channel exists in DB
    if (q->next()) {
      QSharedPointer<Account> dbOwner;
      if (const auto accUid = q->value("account_owner_id").toUuid(); !accUid.isNull()) {
        dbOwner = Account::get_by_uid(accUid);
      }

      QSharedPointer<Server> dbServer;
      if (const auto srvUid = q->value("server_id").toUuid(); !srvUid.isNull()) {
        dbServer = Server::get_by_uid(srvUid);
      }

      return Channel::create_from_db(
        q->value("id").toUuid(),
        q->value("name").toByteArray(),
        q->value("topic").toByteArray(),
        dbOwner,
        dbServer,
        q->value("creation_date").toDateTime()
        );
    }

    // channel does not exist; create new
    if (!server) {
      qCritical() << "channel_get_or_create called with null server for new channel:" << name;
      return nullptr;
    }

    const auto uuid = QUuid::createUuid();

    const auto q_insert = getQuery();
    q_insert->prepare(R"(
      INSERT INTO channels (id, name, topic, account_owner_id, server_id)
      VALUES (?, ?, ?, ?, ?)
    )");
    q_insert->addBindValue(uuid);
    q_insert->addBindValue(name);
    q_insert->addBindValue(topic);
    q_insert->addBindValue(owner ? owner->uid() : QVariant(QMetaType(QMetaType::QByteArray)));
    q_insert->addBindValue(server->uid());

    if (!q_insert->exec()) {
      qCritical() << "channel_get_or_create insert error:" << q_insert->lastError().text();
      return nullptr;
    }

    return Channel::create_from_db(uuid, name, topic, owner, server, QDateTime::currentDateTime());
  }

  QList<QSharedPointer<Channel>> channel_get_all() {
    QList<QSharedPointer<Channel>> channels;
    constexpr int limit = 100;
    int offset = 0;

    while (true) {
      const auto q = getQuery();
      q->prepare("SELECT id, name, topic, account_owner_id, server_id, creation_date FROM channels LIMIT ? OFFSET ?");
      q->addBindValue(limit);
      q->addBindValue(offset);

      if (!q->exec()) {
        qCritical() << "channel_get_all query error:" << q->lastError().text();
        break;
      }

      int rowCount = 0;
      while (q->next()) {
        QSharedPointer<Account> acc;
        if (const auto acc_uid = q->value("account_owner_id").toUuid(); !acc_uid.isNull())
          acc = Account::get_by_uid(acc_uid);

        QSharedPointer<Server> srv;
        if (const auto srv_uid = q->value("server_id").toUuid(); !srv_uid.isNull())
          srv = Server::get_by_uid(srv_uid);

        channels.append(Channel::create_from_db(
          q->value("id").toUuid(),
          q->value("name").toByteArray(),
          q->value("topic").toByteArray(),
          acc,
          srv,
          q->value("creation_date").toDateTime()
          ));
        rowCount++;
      }

      if (rowCount < limit)
        break;
      offset += limit;
    }

    return channels;
  }

  QList<QSharedPointer<Account>> channel_get_members(const QUuid &channel_id) {
    QList<QSharedPointer<Account>> members;

    if (channel_id.isNull()) {
      qCritical() << "channel_get_members called with empty channel_id";
      return members;
    }

    const auto q = getQuery();
    q->prepare(
      "SELECT a.id, a.username, a.password, a.creation_date "
      "FROM accounts a "
      "INNER JOIN account_channels ac ON a.id = ac.account_id "
      "WHERE ac.channel_id = ?"
    );
    q->addBindValue(channel_id); // single placeholder

    if (!q->exec()) {
      qCritical() << "channel_get_members query error:" << q->lastError().text();
      return members;
    }

    while (q->next()) {
      members.append(Account::create_from_db(
        q->value("id").toUuid(),
        q->value("username").toByteArray(),
        q->value("password").toByteArray(),
        q->value("creation_date").toDateTime()
      ));
    }

    return members;
  }

  bool channel_add_member(const QUuid &account_id, const QUuid &channel_id) {
    const auto q = getQuery();
    q->prepare("INSERT INTO account_channels (account_id, channel_id) VALUES (?, ?) ON CONFLICT DO NOTHING");
    q->addBindValue(account_id);
    q->addBindValue(channel_id);

    if (!q->exec()) {
      qCritical() << "channel_add_member error:" << q->lastError().text();
      return false;
    }

    return true;
  }

  bool channel_remove_member(const QUuid &account_id, const QUuid &channel_id) {
    const auto q = getQuery();
    q->prepare("DELETE FROM account_channels WHERE account_id = ? AND channel_id = ?");
    q->addBindValue(account_id);
    q->addBindValue(channel_id);

    if (!q->exec()) {
      qCritical() << "channel_remove_member error:" << q->lastError().text();
      return false;
    }

    return (q->numRowsAffected() > 0);
  }

  bool insertChannel(const QString &name) {
    const auto q = getQuery();
    q->prepare("INSERT INTO channels (name) VALUES (?)");
    q->addBindValue(name);
    if (!q->exec()) {
      qCritical() << "insertChannel error:" << q->lastError().text();
      return false;
    }

    return true;
  }

  LoginResult insertAccount(
      const QString &username, const QString &password, const QString &ip,
      QUuid &rtnAccountID) {
    const auto q = getQuery();
    q->prepare("SELECT id, password FROM accounts WHERE username = ?");
    q->addBindValue(username);

    if (!q->exec()) {
      qCritical() << "insertLogin: query error" << q->lastError().text();
      return LoginResult::DatabaseError;
    }

    if (!q->next())
      return LoginResult::AccountNotFound;

    const QUuid accountId = q->value("id").toUuid();
    const QString storedHash = q->value("password").toString();

    if (!validatePasswordBcrypt(password, storedHash))
      return LoginResult::InvalidPassword;

    const auto q2 = getQuery();
    q2->prepare("INSERT INTO logins (id, account_id, ip) VALUES (?, ?, ?)");
    q2->addBindValue(QUuid::createUuid());
    q2->addBindValue(accountId);
    q2->addBindValue(ip);

    if (!q2->exec()) {
      qCritical() << "insertLogin error:" << q2->lastError().text();
      return LoginResult::DatabaseError;
    }

    rtnAccountID = accountId;
    return LoginResult::Success;
  }

  // SELECT HELPERS
  QList<QVariantMap> getAccounts() {
    QList<QVariantMap> results;
    auto q = exec("SELECT id, username, password, creation_date FROM accounts");
    while (q->next()) {
      QVariantMap row;
      row["id"] = q->value("id");
      row["username"] = q->value("username");
      row["password"] = q->value("password");
      row["creation_date"] = q->value("creation_date");
      results.append(row);
    }
    return results;
  }

  QList<QVariantMap> getChannels() {
    QList<QVariantMap> results;
    auto q = exec("SELECT id, name, creation_date FROM channels");
    while (q->next()) {
      QVariantMap row;
      row["id"] = q->value("id");
      row["name"] = q->value("name");
      row["creation_date"] = q->value("creation_date");
      results.append(row);
    }
    return results;
  }

  QList<QVariantMap> getLogins() {
    QList<QVariantMap> results;
    auto q = exec(R"(
      SELECT logins.id, account_id, ip, login_date, accounts.username
      FROM logins
      JOIN accounts ON accounts.id = logins.account_id
    )");

    while (q->next()) {
      QVariantMap row;
      row["id"] = q->value("id");
      row["account_id"] = q->value("account_id");
      row["username"] = q->value("username");
      row["ip"] = q->value("ip");
      row["login_date"] = q->value("login_date");
      results.append(row);
    }
    return results;
  }

  bool preload_from_file(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      qCritical() << "Failed to open preload file:" << path;
      return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull()) {
      qCritical() << "Failed to parse preload JSON:" << err.errorString();
      return false;
    }

    QJsonObject root = doc.object();

    // --- create default 'admin' user ---
    auto adminAccount = account_get_or_create("admin", "admin");
    if (!adminAccount) {
      qCritical() << "Failed to create default admin account";
      return false;
    }

    QMap<QString, QSharedPointer<Server>> serversByName;

    // --- load servers ---
    for (const auto &val: root["servers"].toArray()) {
      QJsonObject obj = val.toObject();
      QByteArray serverName = obj["name"].toString().toUtf8();

      auto server = server_get_or_create(serverName, adminAccount);
      if (!server) {
        qCritical() << "Failed to create server:" << serverName;
        continue;
      }

      serversByName.insert(QString::fromUtf8(serverName), server);

      // Ensure admin is a member of the server
      server_add_member(adminAccount->uid(), server->uid());
      server->add_account(adminAccount);
    }

    QMap<QString, QSharedPointer<Account>> accountsByName;
    accountsByName.insert("admin", adminAccount);

    // --- Load users ---
    for (const auto &val: root["users"].toArray()) {
      QJsonObject obj = val.toObject();
      QByteArray uname = obj["name"].toString().toUtf8();
      QByteArray pwd = obj["password"].toString().toUtf8();
      QString serverName = obj["server"].toString();

      auto account = account_get_or_create(uname, pwd);
      if (!account) {
        qCritical() << "Failed to create account:" << uname;
        continue;
      }

      accountsByName.insert(QString::fromUtf8(uname), account);

      // Add user to its server
      if (serversByName.contains(serverName)) {
        auto server = serversByName[serverName];
        server_add_member(account->uid(), server->uid());
        server->add_account(account);
      }
    }

    // --- Load channels ---
    for (const auto &val: root["channels"].toArray()) {
      QJsonObject obj = val.toObject();
      QByteArray cname = obj["name"].toString().toUtf8();
      QString ownerName = obj["owner"].toString();
      QByteArray topic = obj["topic"].toString().toUtf8();
      QString serverName = obj["server"].toString();

      QSharedPointer<Account> owner = accountsByName.value(ownerName, nullptr);
      QSharedPointer<Server> server = serversByName.value(serverName, nullptr);

      auto channel = channel_get_or_create(cname, topic, owner, server);
      if (!channel) {
        qCritical() << "Failed to create channel:" << cname;
        continue;
      }

      if (server) {
        server->add_channel(channel);
      }

      // --- Add members to channel and server ---
      for (const auto &mval: obj["members"].toArray()) {
        QString mname = mval.toString();
        if (accountsByName.contains(mname)) {
          auto member = accountsByName[mname];
          channel_add_member(member->uid(), channel->uid);
          if (server)
            server->add_account(member); // Ensure channel member is in server
        } else {
          qWarning() << "Skipping unknown member" << mname << "for channel" << cname;
        }
      }
    }

    return true;
  }

  QString hashPasswordBcrypt(const QString &password) {
    const std::string hash = bcrypt::generateHash(password.toStdString(), 12);
    return QString::fromStdString(hash);
  }

  bool validatePasswordBcrypt(const QString &password, const QString &hash) {
    return bcrypt::validatePassword(password.toStdString(), hash.toStdString());
  }
}