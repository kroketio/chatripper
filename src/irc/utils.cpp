#include "utils.h"

namespace irc {
  QByteArray escapeTagValue(const QByteArray &value) {
    QByteArray escaped;
    escaped.reserve(value.size() * 2); // pre-allocate

    for (int i = 0; i < value.size(); ++i) {
      const char c = value[i];
      switch (c) {
        case ';':
          escaped += "\\:";
          break;
        case ' ':
          escaped += "\\s";
          break;
        case '\\':
          // if this is the last character, drop it (invalid trailing backslash)
          if (i == value.size() - 1)
            break;
          escaped += "\\\\";
          break;
        case '\r':
          escaped += "\\r";
          break;
        case '\n':
          escaped += "\\n";
          break;
        default:
          escaped += c;
          break;
      }
    }

    return escaped;
  }

  QByteArray buildMessageTags(const QSharedPointer<QEventMessage> &message,
                              const QSharedPointer<Account> &src,
                              const Flags<PROTOCOL_CAPABILITY> &capabilities) {
    if (!capabilities.has(PROTOCOL_CAPABILITY::MESSAGE_TAGS))
      return {};

    QList<QByteArray> tags;

    // add account-tag if applicable
    if (capabilities.has(PROTOCOL_CAPABILITY::ACCOUNT_TAG) && !src.isNull()) {
      if (const QByteArray username = src->name(); !username.isEmpty())
        tags.append("account=" + escapeTagValue(username));
    }

    // add message-level tags
    for (auto it = message->tags.begin(); it != message->tags.end(); ++it) {
      const QByteArray key = it.key().toUtf8();
      const QByteArray value = it.value().toByteArray();
      if (value.isEmpty())
        tags.append(key);
      else
        tags.append(key + "=" + escapeTagValue(value));
    }

    if (tags.isEmpty())
      return {};

    QByteArray tag_prefix = "@" + tags.join(";") + " ";

    // enforce IRCv3 tag length limit
    constexpr int max_tag_data = 4094;
    if (tag_prefix.size() > max_tag_data + 2) // +2 for '@' and trailing space
      tag_prefix.truncate(max_tag_data + 2);

    return tag_prefix;
  }

  QMap<QString, QVariant> parseMessageTags(const QByteArray &line, int &tagsEndPos) {
    QMap<QString, QVariant> tags;
    tagsEndPos = -1; // default: no tags found

    if (!line.startsWith('@'))
      return tags;

    // find the end of the tags block (first space)
    const int spaceIdx = line.indexOf(' ');
    if (spaceIdx == -1)
      return tags;

    tagsEndPos = spaceIdx; // mark the end of the tag block
    const QByteArray tagData = line.mid(1, spaceIdx - 1); // exclude '@'
    QList<QByteArray> tagList = tagData.split(';');

    for (const QByteArray &rawTag: tagList) {
      if (rawTag.isEmpty())
        continue;

      const int eqIdx = rawTag.indexOf('=');
      QString key;
      QString value;

      if (eqIdx == -1) {
        key = QString::fromUtf8(rawTag);
        value.clear();
      } else {
        key = QString::fromUtf8(rawTag.left(eqIdx));
        value = QString::fromUtf8(rawTag.mid(eqIdx + 1));
      }

      // unescape value
      QString unescaped;
      for (int i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
          QChar next = value[i + 1];
          switch (next.unicode()) {
            case ':':
              unescaped += ';';
              break;
            case 's':
              unescaped += ' ';
              break;
            case '\\':
              unescaped += '\\';
              break;
            case 'r':
              unescaped += '\r';
              break;
            case 'n':
              unescaped += '\n';
              break;
            default:
              unescaped += next;
              break; // drop invalid escape char
          }
          i++; // skip next char
        } else if (value[i] == '\\') {
          // trailing backslash, ignore
        } else {
          unescaped += value[i];
        }
      }

      // empty values treated as missing
      if (unescaped.isEmpty())
        tags[key] = QVariant();
      else
        tags[key] = QVariant(unescaped);
    }

    return tags;
  }

  QByteArray generateBatchRef() {
    return QUuid::createUuid().toByteArray(QUuid::Id128);
  }

}
