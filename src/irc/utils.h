#pragma once

#include <QByteArray>
#include <QSharedPointer>
#include <QMap>
#include <QRegularExpression>
#include "core/account.h"
#include "core/qtypes.h"

namespace irc {
  /**
   * @brief Validates an IRC nickname according to RFC 2812 and IRCv3 (UTF-8 allowed).
   *
   * A valid nickname:
   *   - Must start with a letter or one of [ ] \ ` _ ^ { | }
   *   - May contain letters, digits, or [ ] \ ` _ ^ { | } -
   *   - Must be at most 9 characters long
   *   - May include UTF-8 letters and digits as per IRCv3
   *
   * @param nick The nickname to validate (UTF-8 encoded).
   * @return bool True if the nickname is valid, false otherwise.
   */
  inline bool isValidNick(const QByteArray &nick) {
    const QString s = QString::fromUtf8(nick);
    static QRegularExpression re(
      "^[\\p{L}\\[\\]\\\\`_^{|}][\\p{L}\\p{N}\\[\\]\\\\`_^{|}-]{0,8}$"
    );
    return re.match(s).hasMatch();
  }

  /**
   * @brief Escapes a message tag value according to IRCv3 message-tags specification.
   *
   * Certain characters in tag values must be escaped:
   *   - ';'  → '\:'
   *   - SPACE → '\s'
   *   - '\'  → '\\'
   *   - CR   → '\r'
   *   - LF   → '\n'
   *
   * @param value The raw tag value to escape.
   * @return QByteArray The escaped tag value ready for inclusion in an IRCv3 message.
   */
  static QByteArray escapeTagValue(const QByteArray &value);

  /**
   * @brief Builds the IRCv3 message tag prefix for a given message.
   *
   * This function generates the '@tag1=value1;tag2=value2 ... ' prefix for a message.
   * It includes:
   *   - The 'account' tag if the sender is logged in and ACCOUNT_TAG capability is enabled.
   *   - Any additional tags present in QEventMessage::tags.
   *
   * The resulting string is empty if the MESSAGE_TAGS capability is not negotiated
   * or if no tags are present.
   *
   * @param message Pointer to the QEventMessage containing user-defined tags.
   * @param src Pointer to the sender Account object (used for account-tag).
   * @param capabilities Flags indicating which protocol capabilities are enabled for the client.
   * @return QByteArray The full tag prefix, including leading '@' and trailing space,
   *                   or an empty QByteArray if no tags are to be sent.
   */
  QByteArray buildMessageTags(
      const QSharedPointer<QEventMessage> &message,
      const QSharedPointer<Account> &src,
      const Flags<PROTOCOL_CAPABILITY> &capabilities
      );

  QMap<QString, QVariant> parseMessageTags(const QByteArray &line, int &tagsEndPos);
}
