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

  QByteArray buildTagPrefix(const QSharedPointer<QEventMessage> &message,
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

}
