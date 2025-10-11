#pragma once

#include <QByteArray>
#include <QSharedPointer>
#include <QMap>
#include "core/account.h"
#include "core/qtypes.h"

namespace irc {
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
  QByteArray buildTagPrefix(
      const QSharedPointer<QEventMessage> &message,
      const QSharedPointer<Account> &src,
      const Flags<PROTOCOL_CAPABILITY> &capabilities
      );

}
