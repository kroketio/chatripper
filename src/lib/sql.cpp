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

// define the static member
QSqlDatabase SQL::db = QSqlDatabase();

QSqlDatabase &SQL::getInstance() {
  if (!db.isValid()) {
    db = QSqlDatabase::addDatabase("QSQLITE");
    qDebug() << "Database instance created.";
  }
  return db;
}

bool SQL::initialize() {
  const auto path_db = g::pathDatabase.filePath();
  QSqlDatabase &db = getInstance();
  db.setDatabaseName(path_db);

  if (!db.open()) {
    qDebug() << "Error: Unable to open the database!" << db.lastError().text();
    return false;
  }

  create_schema();
  qDebug() << "Database opened successfully: " << path_db;
  return true;
}

QSqlQuery SQL::exec(const QString &sql) {
  const QSqlDatabase &db = SQL::getInstance();
  QSqlQuery q(db);
  const auto res = q.exec(sql);
  if (!res) {
    const auto err = q.lastError().text();
    if (!err.contains("already exists"))
      qCritical() << "SQL error: " << err;
  }
  return q;
}

QSqlQuery &SQL::exec(QSqlQuery &q) {
  if (auto res = q.exec(); !res) {
    if (auto err = q.lastError().text(); !err.contains("already exists"))
      qCritical() << "SQL error: " << err;
  }
  return q;
}

// ----------------------
// SCHEMA CREATION
// ----------------------
void SQL::create_schema() {
  exec(R"(
    CREATE TABLE IF NOT EXISTS accounts (
      id BLOB PRIMARY KEY,
      username TEXT UNIQUE NOT NULL,
      password TEXT NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS channels (
      id BLOB PRIMARY KEY,
      name TEXT UNIQUE NOT NULL,
      account_owner_id BLOB,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS account_channels (
      account_id BLOB NOT NULL,
      channel_id BLOB NOT NULL,
      PRIMARY KEY(account_id, channel_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id),
      FOREIGN KEY(channel_id) REFERENCES channels(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS logins (
      id BLOB PRIMARY KEY,
      account_id BLOB NOT NULL,
      ip TEXT NOT NULL,
      login_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_id) REFERENCES accounts(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS events (
      id BLOB PRIMARY KEY,
      account_id BLOB NOT NULL,
      channel_id BLOB,
      recipient_id BLOB,
      event_type INTEGER NOT NULL,
      data TEXT NOT NULL,
      reply_to BLOB,
      display_name TEXT,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_id) REFERENCES accounts(id),
      FOREIGN KEY(channel_id) REFERENCES channels(id),
      FOREIGN KEY(recipient_id) REFERENCES accounts(id),
      FOREIGN KEY(reply_to) REFERENCES events(id)
    );
  )");

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
}

// INSERT HELPERS
QSharedPointer<Account> SQL::account_get_or_create(const QByteArray &username, const QByteArray &password) {
  auto it = g::ctx->accounts_lookup_name.find(username);
  if (it != g::ctx->accounts_lookup_name.end() && !it.value().isNull()) {
    return it.value();
  }

  QSqlQuery q(getInstance());
  q.prepare("SELECT id, username, password, creation_date FROM accounts WHERE username = ?");
  q.addBindValue(username);

  if (!q.exec()) {
    qCritical() << "account_get_or_create select error:" << q.lastError().text();
    return nullptr;
  }

  if (q.next()) {
    return Account::create_from_db(
      q.value("id").toByteArray(),
      q.value("username").toByteArray(),
      q.value("password").toByteArray(),
      q.value("creation_date").toDateTime()
    );
  }

  // create new account
  QByteArray newId = generateUuid();
  QString hashedPassword = hashPasswordBcrypt(QString::fromUtf8(password));

  QSqlQuery insertQuery(getInstance());
  insertQuery.prepare("INSERT INTO accounts (id, username, password) VALUES (?, ?, ?)");
  insertQuery.addBindValue(newId);
  insertQuery.addBindValue(username);
  insertQuery.addBindValue(hashedPassword);

  if (!insertQuery.exec()) {
    qCritical() << "account_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Account::create_from_db(newId, username, hashedPassword.toUtf8(), QDateTime::currentDateTime());
}

bool SQL::account_exists(const QByteArray &username) {
  if (g::ctx->accounts_lookup_name.contains(username))
    return true;

  QSqlQuery q(getInstance());
  q.prepare("SELECT 1 FROM accounts WHERE username = ? LIMIT 1");
  q.addBindValue(username);

  if (!q.exec()) {
    qCritical() << "accountExists query error:" << q.lastError().text();
    return false;
  }

  return q.next();
}

QList<QSharedPointer<Channel>> SQL::account_get_channels(const QByteArray &account_id) {
  QList<QSharedPointer<Channel>> channels;

  if (account_id.isEmpty()) {
    qCritical() << "account_get_channels called with empty account_id";
    return channels;
  }

  QSqlQuery q(getInstance());
  q.prepare(R"(
    SELECT c.id, c.name, c.account_owner_id, c.creation_date
    FROM channels c
    INNER JOIN account_channels ac ON c.id = ac.channel_id
    WHERE ac.account_id = ?
  )");
  q.addBindValue(account_id);

  if (!q.exec()) {
    qCritical() << "account_get_channels query error:" << q.lastError().text();
    return channels;
  }

  while (q.next()) {
    channels.append(Channel::create_from_db(
      q.value("id").toByteArray(),
      q.value("name").toByteArray(),
      q.value("account_owner_id").toByteArray(),
      q.value("creation_date").toDateTime()
    ));
  }

  return channels;
}

QList<QSharedPointer<Account>> SQL::account_get_all() {
  QList<QSharedPointer<Account>> accounts;
  const int batchSize = 100;
  int offset = 0;

  while (true) {
    QSqlQuery q(getInstance());
    q.prepare("SELECT id, username, password, creation_date FROM accounts LIMIT ? OFFSET ?");
    q.addBindValue(batchSize);
    q.addBindValue(offset);

    if (!q.exec()) {
      qCritical() << "getAllAccounts query error:" << q.lastError().text();
      break;
    }

    int rowCount = 0;
    while (q.next()) {
      accounts.append(Account::create_from_db(
        q.value("id").toByteArray(),
        q.value("username").toByteArray(),
        q.value("password").toByteArray(),
        q.value("creation_date").toDateTime()
      ));
      rowCount++;
    }

    if (rowCount < batchSize) break;
    offset += batchSize;
  }

  return accounts;
}

bool SQL::channel_exists(const QByteArray &name) {
  if (g::ctx->channels.contains(name))
    return true;

  QSqlQuery q(getInstance());
  q.prepare("SELECT 1 FROM channels WHERE name = ? LIMIT 1");
  q.addBindValue(name);

  if (!q.exec()) {
    qCritical() << "channel_exists query error:" << q.lastError().text();
    return false;
  }

  return q.next();
}

QSharedPointer<Channel> SQL::channel_get_or_create(const QByteArray &name, const QByteArray &account_owner_id) {
  auto it = g::ctx->channels.find(name);
  if (it != g::ctx->channels.end() && !it.value().isNull()) {
    return it.value();
  }

  QSqlQuery q(getInstance());
  q.prepare("SELECT id, name, account_owner_id, creation_date FROM channels WHERE name = ?");
  q.addBindValue(name);

  if (!q.exec()) {
    qCritical() << "channel_get_or_create select error:" << q.lastError().text();
    return nullptr;
  }

  if (q.next()) {
    return Channel::create_from_db(
      q.value("id").toByteArray(),
      q.value("name").toByteArray(),
      q.value("account_owner_id").toByteArray(),
      q.value("creation_date").toDateTime()
    );
  }

  QByteArray newId = generateUuid();

  QSqlQuery insertQuery(getInstance());
  insertQuery.prepare("INSERT INTO channels (id, name, account_owner_id) VALUES (?, ?, ?)");
  insertQuery.addBindValue(newId);
  insertQuery.addBindValue(name);
  insertQuery.addBindValue(account_owner_id.isEmpty() ? QVariant(QVariant::ByteArray) : QVariant(account_owner_id));

  if (!insertQuery.exec()) {
    qCritical() << "channel_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Channel::create_from_db(newId, name, account_owner_id, QDateTime::currentDateTime());
}

QList<QSharedPointer<Channel>> SQL::channel_get_all() {
  QList<QSharedPointer<Channel>> channels;
  constexpr int limit = 100;
  int offset = 0;

  while (true) {
    QSqlQuery q(getInstance());
    q.prepare("SELECT id, name, account_owner_id, creation_date FROM channels LIMIT ? OFFSET ?");
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
      qCritical() << "channel_get_all query error:" << q.lastError().text();
      break;
    }

    int rowCount = 0;
    while (q.next()) {
      channels.append(Channel::create_from_db(
        q.value("id").toByteArray(),
        q.value("name").toByteArray(),
        q.value("account_owner_id").toByteArray(),
        q.value("creation_date").toDateTime()
      ));
      rowCount++;
    }

    if (rowCount < limit) break;
    offset += limit;
  }

  return channels;
}

QList<QSharedPointer<Account>> SQL::channel_get_members(const QByteArray &channel_id) {
  QList<QSharedPointer<Account>> members;

  if (channel_id.isEmpty()) {
    qCritical() << "channel_get_members called with empty channel_id";
    return members;
  }

  QSqlQuery q(getInstance());
  q.prepare(R"(
        SELECT a.id, a.username, a.password, a.creation_date
        FROM accounts a
        INNER JOIN account_channels ac ON a.id = ac.account_id
        WHERE ac.channel_id = ?
    )");
  q.addBindValue(channel_id);

  if (!q.exec()) {
    qCritical() << "channel_get_members query error:" << q.lastError().text();
    return members;
  }

  while (q.next()) {
    members.append(Account::create_from_db(
      q.value("id").toByteArray(),
      q.value("username").toByteArray(),
      q.value("password").toByteArray(),
      q.value("creation_date").toDateTime()
    ));
  }

  return members;
}

bool SQL::channel_add_member(const QByteArray &account_id, const QByteArray &channel_id) {
  if (account_id.isEmpty() || channel_id.isEmpty()) {
    qCritical() << "channel_add_member called with empty account_id or channel_id";
    return false;
  }

  QSqlQuery q(getInstance());
  q.prepare("INSERT OR IGNORE INTO account_channels (account_id, channel_id) VALUES (?, ?)");
  q.addBindValue(account_id);
  q.addBindValue(channel_id);

  if (!q.exec()) {
    qCritical() << "channel_add_member error:" << q.lastError().text();
    return false;
  }

  return true;
}

bool SQL::channel_remove_member(const QByteArray &account_id, const QByteArray &channel_id) {
  if (account_id.isEmpty() || channel_id.isEmpty()) {
    qCritical() << "channel_remove_member called with empty account_id or channel_id";
    return false;
  }

  QSqlQuery q(getInstance());
  q.prepare("DELETE FROM account_channels WHERE account_id = ? AND channel_id = ?");
  q.addBindValue(account_id);
  q.addBindValue(channel_id);

  if (!q.exec()) {
    qCritical() << "channel_remove_member error:" << q.lastError().text();
    return false;
  }

  return (q.numRowsAffected() > 0);
}

bool SQL::insertChannel(const QString &name) {
  QSqlQuery q(getInstance());
  q.prepare("INSERT INTO channels (name) VALUES (?)");
  q.addBindValue(name);
  if (!q.exec()) {
    qCritical() << "insertChannel error:" << q.lastError().text();
    return false;
  }

  return true;
}

SQL::LoginResult SQL::insertAccount(
    const QString &username, const QString &password, const QString &ip,
    QByteArray &rtnAccountID) {
  rtnAccountID.clear();

  QSqlQuery q(getInstance());
  q.prepare("SELECT id, password FROM accounts WHERE username = ?");
  q.addBindValue(username);

  if (!q.exec()) {
    qCritical() << "insertLogin: query error" << q.lastError().text();
    return LoginResult::DatabaseError;
  }

  if (!q.next()) {
    return LoginResult::AccountNotFound;
  }

  const QByteArray accountId = q.value("id").toByteArray();
  const QString storedHash = q.value("password").toString();

  if (!validatePasswordBcrypt(password, storedHash)) {
    return LoginResult::InvalidPassword;
  }

  QSqlQuery q2(getInstance());
  q2.prepare("INSERT INTO logins (id, account_id, ip) VALUES (?, ?, ?)");
  q2.addBindValue(generateUuid());
  q2.addBindValue(accountId);
  q2.addBindValue(ip);

  if (!q2.exec()) {
    qCritical() << "insertLogin error:" << q2.lastError().text();
    return LoginResult::DatabaseError;
  }

  rtnAccountID = accountId;
  return LoginResult::Success;
}

// SELECT HELPERS
QList<QVariantMap> SQL::getAccounts() {
  QList<QVariantMap> results;
  QSqlQuery q = exec("SELECT id, username, password, creation_date FROM accounts");
  while (q.next()) {
    QVariantMap row;
    row["id"] = q.value("id");
    row["username"] = q.value("username");
    row["password"] = q.value("password");
    row["creation_date"] = q.value("creation_date");
    results.append(row);
  }
  return results;
}

QList<QVariantMap> SQL::getChannels() {
  QList<QVariantMap> results;
  QSqlQuery q = exec("SELECT id, name, creation_date FROM channels");
  while (q.next()) {
    QVariantMap row;
    row["id"] = q.value("id");
    row["name"] = q.value("name");
    row["creation_date"] = q.value("creation_date");
    results.append(row);
  }
  return results;
}

QList<QVariantMap> SQL::getLogins() {
  QList<QVariantMap> results;
  QSqlQuery q = exec(R"(
    SELECT logins.id, account_id, ip, login_date, accounts.username
    FROM logins
    JOIN accounts ON accounts.id = logins.account_id
  )");
  while (q.next()) {
    QVariantMap row;
    row["id"] = q.value("id");
    row["account_id"] = q.value("account_id");
    row["username"] = q.value("username");
    row["ip"] = q.value("ip");
    row["login_date"] = q.value("login_date");
    results.append(row);
  }
  return results;
}

bool SQL::preload_from_file(const QString &path) {
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

  // --- Load accounts ---
  QMap<QString, QSharedPointer<Account>> accountsByName;
  QJsonArray users = root["users"].toArray();
  for (const QJsonValue &val: users) {
    QJsonObject obj = val.toObject();
    QByteArray uname = obj["name"].toString().toUtf8();
    QByteArray pwd = obj["password"].toString().toUtf8();

    auto account = account_get_or_create(uname, pwd);
    if (account) {
      accountsByName.insert(QString::fromUtf8(uname), account);
    }
  }

  // --- Load channels ---
  QJsonArray channels = root["channels"].toArray();
  for (const QJsonValue &val: channels) {
    QJsonObject obj = val.toObject();
    QByteArray cname = obj["name"].toString().toUtf8();
    QString ownerName = obj["owner"].toString();

    QByteArray ownerId;
    if (accountsByName.contains(ownerName)) {
      ownerId = accountsByName[ownerName]->uid;
    }

    auto channel = channel_get_or_create(cname, ownerId);
    if (!channel) {
      qCritical() << "Failed to create channel:" << cname;
      continue;
    }

    // add members
    QJsonArray members = obj["members"].toArray();
    for (const QJsonValue &mval: members) {
      QString mname = mval.toString();
      if (accountsByName.contains(mname)) {
        SQL::channel_add_member(accountsByName[mname]->uid, channel->uid);
      } else {
        qWarning() << "Skipping unknown member" << mname << "for channel" << cname;
      }
    }
  }

  return true;
}

QByteArray SQL::generateUuid() {
  const QUuid uuid = QUuid::createUuid();
  // 16-byte QByteArray (big-endian, RFC 4122)
  return uuid.toRfc4122();
}

QString SQL::uuidToString(const QByteArray &uuidBytes) {
  const QUuid uuid = QUuid::fromRfc4122(uuidBytes);
  return uuid.toString(QUuid::WithoutBraces);
}

QString SQL::hashPasswordBcrypt(const QString &password) {
  const std::string hash = bcrypt::generateHash(password.toStdString(), 12);
  return QString::fromStdString(hash);
}

bool SQL::validatePasswordBcrypt(const QString &password, const QString &hash) {
  return bcrypt::validatePassword(password.toStdString(), hash.toStdString());
}
