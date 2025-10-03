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
    CREATE TABLE IF NOT EXISTS servers (
      id BLOB PRIMARY KEY,
      name TEXT NOT NULL,
      account_owner_id BLOB NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS server_members (
      account_id BLOB NOT NULL,
      server_id BLOB NOT NULL,
      join_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(account_id, server_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id),
      FOREIGN KEY(server_id) REFERENCES servers(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS channels (
      id BLOB PRIMARY KEY,
      server_id BLOB NOT NULL,
      name TEXT NOT NULL,
      topic TEXT,
      account_owner_id BLOB,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(server_id) REFERENCES servers(id),
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id),
      UNIQUE(server_id, name)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS uploads (
      id BLOB PRIMARY KEY,
      account_owner_id BLOB NOT NULL,
      path TEXT NOT NULL,
      type INTEGER NOT NULL,
      variant INTEGER NOT NULL,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(account_owner_id) REFERENCES accounts(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS roles (
      id BLOB PRIMARY KEY,
      server_id BLOB NOT NULL,
      name TEXT NOT NULL,
      icon BLOB,
      color INTEGER,
      priority INTEGER DEFAULT 0,
      creation_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY(server_id) REFERENCES servers(id),
      FOREIGN KEY(icon) REFERENCES uploads(id),
      UNIQUE(server_id, name)
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
    CREATE TABLE IF NOT EXISTS account_roles (
      account_id BLOB NOT NULL,
      role_id BLOB NOT NULL,
      PRIMARY KEY(account_id, role_id),
      FOREIGN KEY(account_id) REFERENCES accounts(id),
      FOREIGN KEY(role_id) REFERENCES roles(id)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS permissions (
      id BLOB PRIMARY KEY,
      role_id BLOB NOT NULL,
      permission_bits INTEGER NOT NULL DEFAULT 0,
      FOREIGN KEY(role_id) REFERENCES roles(id)
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
    )
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

// INSERT HELPERS
QSharedPointer<Account> SQL::account_get_or_create(const QByteArray &username, const QByteArray &password) {
  auto it = g::ctx->accounts_lookup_name.find(username);
  if (it != g::ctx->accounts_lookup_name.end() && !it.value().isNull()) {
    return it.value();
  }

  QSqlQuery q(getInstance());
  q.prepare("SELECT id, username, password, creation_date FROM accounts WHERE username = ?");
  q.addBindValue(QString::fromUtf8(username));

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
  insertQuery.addBindValue(QString::fromUtf8(username));
  insertQuery.addBindValue(hashedPassword);

  if (!insertQuery.exec()) {
    qCritical() << "account_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Account::create_from_db(newId, username, hashedPassword.toUtf8(), QDateTime::currentDateTime());
}

QSharedPointer<Upload> SQL::upload_get_or_create(
    const QByteArray &accountId,
    const QString &path,
    int type,
    int variant) {
  QSqlQuery q(getInstance());
  q.prepare(
      "SELECT id, account_owner_id, path, type, variant, creation_date FROM uploads WHERE account_owner_id = ? AND path = ?");
  q.addBindValue(accountId);
  q.addBindValue(path);

  if (!q.exec()) {
    qCritical() << "upload_get_or_create select error:" << q.lastError().text();
    return nullptr;
  }

  if (q.next()) {
    return Upload::create_from_db(
        q.value("id").toByteArray(),
        q.value("account_owner_id").toByteArray(),
        q.value("path").toString(),
        q.value("type").toInt(),
        q.value("variant").toInt(),
        q.value("creation_date").toDateTime()
        );
  }

  QByteArray newId = generateUuid();

  QSqlQuery insertQuery(getInstance());
  insertQuery.prepare("INSERT INTO uploads (id, account_owner_id, path, type, variant) VALUES (?, ?, ?, ?, ?)");
  insertQuery.addBindValue(newId);
  insertQuery.addBindValue(accountId);
  insertQuery.addBindValue(path);
  insertQuery.addBindValue(type);
  insertQuery.addBindValue(variant);

  if (!insertQuery.exec()) {
    qCritical() << "upload_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Upload::create_from_db(newId, accountId, path, type, variant, QDateTime::currentDateTime());
}

QSharedPointer<Permission> SQL::permission_get_or_create(const QByteArray &roleId, Permission::PermissionFlags flags) {
  QSqlQuery q(getInstance());
  q.prepare("SELECT id, role_id, permission_bits FROM permissions WHERE role_id = ?");
  q.addBindValue(roleId);

  if (!q.exec()) {
    qCritical() << "permission_get_or_create select error:" << q.lastError().text();
    return nullptr;
  }

  if (q.next()) {
    return Permission::create_from_db(
        q.value("id").toByteArray(),
        q.value("role_id").toByteArray(),
        q.value("permission_bits").toInt()
        );
  }

  QByteArray newId = generateUuid();

  QSqlQuery insertQuery(getInstance());
  insertQuery.prepare("INSERT INTO permissions (id, role_id, permission_bits) VALUES (?, ?, ?)");
  insertQuery.addBindValue(newId);
  insertQuery.addBindValue(roleId);
  insertQuery.addBindValue(flags.bits);

  if (!insertQuery.exec()) {
    qCritical() << "permission_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Permission::create_from_db(newId, roleId, flags.bits);
}

QSharedPointer<Role> SQL::create_role_for_server(
    const QSharedPointer<Server> &server,
    const QString &roleName,
    int priority,
    const QByteArray &iconId,
    bool assignToExistingMembers,
    Permission::PermissionFlags defaultPermissions)
{
    if (!server || server->uid().isEmpty())
        return nullptr;

    // -------------------------
    // Check if role already exists
    // -------------------------
    QSqlQuery selectQuery(getInstance());
    selectQuery.prepare(
        "SELECT id, server_id, name, icon, color, priority, creation_date "
        "FROM roles WHERE server_id = ? AND name = ?"
    );
    selectQuery.addBindValue(server->uid());
    selectQuery.addBindValue(roleName);

    if (!selectQuery.exec()) {
        qCritical() << "create_role_for_server select error:" << selectQuery.lastError().text();
        return nullptr;
    }

    if (selectQuery.next()) {
        // Role already exists, add to server cache if not present
        QByteArray existingRoleId = selectQuery.value("id").toByteArray();
        auto role = Role::create_from_db(
            existingRoleId,
            selectQuery.value("server_id").toByteArray(),
            selectQuery.value("name").toByteArray(),
            selectQuery.value("icon").toByteArray(),
            selectQuery.value("color").toInt(),
            selectQuery.value("priority").toInt(),
            selectQuery.value("creation_date").toDateTime()
        );

        server->add_role(role); // ensure role is cached
        return role;
    }

    // -------------------------
    // Create new role
    // -------------------------
    QByteArray newRoleId = generateUuid();
    QSqlQuery insertQuery(getInstance());
    insertQuery.prepare(
        "INSERT INTO roles (id, server_id, name, priority, icon) VALUES (?, ?, ?, ?, ?)"
    );
    insertQuery.addBindValue(newRoleId);       // BLOB
    insertQuery.addBindValue(server->uid());   // BLOB
    insertQuery.addBindValue(roleName);        // TEXT
    insertQuery.addBindValue(priority);        // INTEGER
    if (iconId.isEmpty()) {
        insertQuery.addBindValue(QVariant(QMetaType(QMetaType::QByteArray))); // NULL BLOB
    } else {
        insertQuery.addBindValue(iconId); // BLOB
    }

    if (!insertQuery.exec()) {
        qCritical() << "create_role_for_server insert error:" << insertQuery.lastError().text();
        return nullptr;
    }

    // -------------------------
    // Fetch the newly created role to get creation_date
    // -------------------------
    QSqlQuery fetchQuery(getInstance());
    fetchQuery.prepare(
        "SELECT id, server_id, name, icon, color, priority, creation_date FROM roles WHERE id = ?"
    );
    fetchQuery.addBindValue(newRoleId);

    if (!fetchQuery.exec() || !fetchQuery.next()) {
        qCritical() << "Failed to fetch newly created role:" << fetchQuery.lastError().text();
        return nullptr;
    }

    auto role = Role::create_from_db(
        fetchQuery.value("id").toByteArray(),
        fetchQuery.value("server_id").toByteArray(),
        fetchQuery.value("name").toByteArray(),
        fetchQuery.value("icon").toByteArray(),
        fetchQuery.value("color").toInt(),
        fetchQuery.value("priority").toInt(),
        fetchQuery.value("creation_date").toDateTime()
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
        for (auto &member : server->all_accounts()) {
            QSqlQuery ar(getInstance());
            ar.prepare("INSERT OR IGNORE INTO account_roles (account_id, role_id) VALUES (?, ?)");
            ar.addBindValue(member->uid());
            ar.addBindValue(newRoleId);
            if (!ar.exec()) {
                qWarning() << "Failed to assign role" << roleName
                           << "to member" << member->name() << ":" << ar.lastError().text();
            }
        }
    }

    return role;
}

bool SQL::assign_role_to_account(
    const QByteArray &accountId,
    const QSharedPointer<Role> &role) {
  if (!role || accountId.isEmpty())
    return false;

  QSqlQuery q(getInstance());
  q.prepare("INSERT OR IGNORE INTO account_roles (account_id, role_id) VALUES (?, ?)");
  q.addBindValue(accountId);
  q.addBindValue(role->uid());

  if (!q.exec()) {
    qWarning() << "Failed to assign role" << role->name()
        << "to account" << accountId
        << ":" << q.lastError().text();
    return false;
  }

  return true;
}


bool SQL::server_add_member(const QByteArray &accountId, const QByteArray &serverId) {
  if (accountId.isEmpty() || serverId.isEmpty()) {
    qCritical() << "server_add_member called with empty accountId or serverId";
    return false;
  }

  // prevent duplicate membership
  QSqlQuery check(SQL::getInstance());
  check.prepare("SELECT 1 FROM server_members WHERE account_id = ? AND server_id = ?");
  check.addBindValue(accountId);
  check.addBindValue(serverId);
  if (!check.exec()) {
    qCritical() << "server_add_member check error:" << check.lastError().text();
    return false;
  }
  if (check.next()) {
    // already a member
    return true;
  }

  // insert into server_members table
  QSqlQuery q(getInstance());
  q.prepare("INSERT INTO server_members (account_id, server_id) VALUES (?, ?)");
  q.addBindValue(accountId);
  q.addBindValue(serverId);
  if (!q.exec()) {
    qCritical() << "server_add_member insert error:" << q.lastError().text();
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

QSharedPointer<Server> SQL::server_get_or_create(const QByteArray &name, const QSharedPointer<Account> &owner) {
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
    QSqlQuery q(getInstance());
    q.prepare("SELECT id, name, account_owner_id, creation_date FROM servers WHERE name = ?");
    q.addBindValue(name);

    if (!q.exec()) {
        qCritical() << "server_get_or_create select error:" << q.lastError().text();
        return nullptr;
    }

    if (q.next()) {
        const QByteArray ownerId = q.value("account_owner_id").toByteArray();
        QSharedPointer<Account> ownerPtr = Account::get_by_uid(ownerId);

        server = Server::create_from_db(
            q.value("id").toByteArray(),
            q.value("name").toByteArray(),
            ownerPtr,
            q.value("creation_date").toDateTime()
        );
    } else {
        // --- Create server in DB ---
        QByteArray newId = generateUuid();
        QSqlQuery insertQuery(getInstance());
        insertQuery.prepare("INSERT INTO servers (id, name, account_owner_id) VALUES (?, ?, ?)");
        insertQuery.addBindValue(newId);
        insertQuery.addBindValue(name);
        insertQuery.addBindValue(owner->uid());

        if (!insertQuery.exec()) {
            qCritical() << "server_get_or_create insert error:" << insertQuery.lastError().text();
            return nullptr;
        }

        server = Server::create_from_db(newId, name, owner, QDateTime::currentDateTime());
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
    QSqlQuery membersQuery(getInstance());
    membersQuery.prepare("SELECT account_id FROM server_members WHERE server_id = ?");
    membersQuery.addBindValue(server->uid());
    if (membersQuery.exec()) {
        QReadLocker locker(&g::ctx->mtx_cache);
        while (membersQuery.next()) {
            const QByteArray acc_id = membersQuery.value("account_id").toByteArray();
            if (g::ctx->accounts_lookup_uuid.contains(acc_id)) {
                server->add_account(g::ctx->accounts_lookup_uuid[acc_id]);
            }
        }
    }

    return server;
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
    SELECT c.id, c.name, c.topic, c.account_owner_id, c.server_id, c.creation_date
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
    QSharedPointer<Account> acc;
    if (const auto acc_uid = q.value("account_owner_id").toByteArray(); !acc_uid.isEmpty())
      acc = Account::get_by_uid(acc_uid);

    QSharedPointer<Server> server;
    if (const auto server_uid = q.value("server_id").toByteArray(); !server_uid.isEmpty())
      server = Server::get_by_uid(server_uid);

    channels.append(Channel::create_from_db(
        q.value("id").toByteArray(),
        q.value("name").toByteArray(),
        q.value("topic").toByteArray(),
        acc,
        server,
        q.value("creation_date").toDateTime()
        ));
  }

  return channels;
}

QList<QSharedPointer<Account>> SQL::account_get_all() {
  QList<QSharedPointer<Account>> accounts;
  const int batch_size = 100;
  int offset = 0;

  while (true) {
    QSqlQuery q(getInstance());
    q.prepare("SELECT id, username, password, creation_date FROM accounts LIMIT ? OFFSET ?");
    q.addBindValue(batch_size);
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

    if (rowCount < batch_size)
      break;
    offset += batch_size;
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

QSharedPointer<Channel> SQL::channel_get_or_create(
    const QByteArray &name,
    const QByteArray &topic,
    const QSharedPointer<Account> &owner,
    const QSharedPointer<Server> &server) {
  // check in-memory cache first
  if (const auto it = g::ctx->channels.find(name); it != g::ctx->channels.end() && !it.value().isNull())
    return it.value();

  QSqlQuery query(getInstance());
  query.prepare(R"(
        SELECT id, name, topic, account_owner_id, server_id, creation_date
        FROM channels
        WHERE name = ?
    )");
  query.addBindValue(name);

  if (!query.exec()) {
    qCritical() << "channel_get_or_create select error:" << query.lastError().text();
    return nullptr;
  }

  // channel exists in DB
  if (query.next()) {
    QSharedPointer<Account> dbOwner;
    if (const auto accUid = query.value("account_owner_id").toByteArray(); !accUid.isEmpty()) {
      dbOwner = Account::get_by_uid(accUid);
    }

    QSharedPointer<Server> dbServer;
    if (const auto srvUid = query.value("server_id").toByteArray(); !srvUid.isEmpty()) {
      dbServer = Server::get_by_uid(srvUid);
    }

    return Channel::create_from_db(
        query.value("id").toByteArray(),
        query.value("name").toByteArray(),
        query.value("topic").toByteArray(),
        dbOwner,
        dbServer,
        query.value("creation_date").toDateTime()
        );
  }

  // channel does not exist; create new
  if (!server) {
    qCritical() << "channel_get_or_create called with null server for new channel:" << name;
    return nullptr;
  }

  QByteArray newId = generateUuid();

  QSqlQuery insertQuery(getInstance());
  insertQuery.prepare(R"(
        INSERT INTO channels (id, name, topic, account_owner_id, server_id)
        VALUES (?, ?, ?, ?, ?)
    )");
  insertQuery.addBindValue(newId);
  insertQuery.addBindValue(name);
  insertQuery.addBindValue(topic);
  insertQuery.addBindValue(owner ? owner->uid() : QVariant(QMetaType(QMetaType::QByteArray)));
  insertQuery.addBindValue(server->uid());

  if (!insertQuery.exec()) {
    qCritical() << "channel_get_or_create insert error:" << insertQuery.lastError().text();
    return nullptr;
  }

  return Channel::create_from_db(newId, name, topic, owner, server, QDateTime::currentDateTime());
}

QList<QSharedPointer<Channel>> SQL::channel_get_all() {
  QList<QSharedPointer<Channel>> channels;
  constexpr int limit = 100;
  int offset = 0;

  while (true) {
    QSqlQuery q(getInstance());
    q.prepare("SELECT id, name, topic, account_owner_id, server_id, creation_date FROM channels LIMIT ? OFFSET ?");
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
      qCritical() << "channel_get_all query error:" << q.lastError().text();
      break;
    }

    int rowCount = 0;
    while (q.next()) {
      QSharedPointer<Account> acc;
      if (const auto acc_uid = q.value("account_owner_id").toByteArray(); !acc_uid.isEmpty())
        acc = Account::get_by_uid(acc_uid);

      QSharedPointer<Server> srv;
      if (const auto srv_uid = q.value("server_id").toByteArray(); !srv_uid.isEmpty())
        srv = Server::get_by_uid(srv_uid);

      channels.append(Channel::create_from_db(
          q.value("id").toByteArray(),
          q.value("name").toByteArray(),
          q.value("topic").toByteArray(),
          acc,
          srv,
          q.value("creation_date").toDateTime()
          ));
      rowCount++;
    }

    if (rowCount < limit)
      break;
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
  q.prepare(
      "SELECT a.id, a.username, a.password, a.creation_date "
      "FROM accounts a "
      "INNER JOIN account_channels ac ON a.id = ac.account_id "
      "WHERE ac.channel_id = ?"
  );
  q.addBindValue(channel_id); // single placeholder

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

  // --- Create default 'admin' user ---
  auto adminAccount = account_get_or_create("admin", "admin");
  if (!adminAccount) {
    qCritical() << "Failed to create default admin account";
    return false;
  }

  QMap<QString, QSharedPointer<Server>> serversByName;

  // --- Load servers ---
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
    SQL::server_add_member(adminAccount->uid(), server->uid());
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
      SQL::server_add_member(account->uid(), server->uid());
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
        SQL::channel_add_member(member->uid(), channel->uid);
        if (server)
          server->add_account(member); // Ensure channel member is in server
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
