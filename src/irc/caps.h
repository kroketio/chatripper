#pragma once

namespace irc {
  enum class PROTOCOL_CAPABILITY {
    MULTI_PREFIX,    // https://ircv3.net/specs/extensions/multi-prefix
    MESSAGE_TAGS,    // https://ircv3.net/specs/extensions/message-tags
    EXTENDED_JOIN,   //
    CHGHOST,         // https://ircv3.net/specs/extensions/chghost
    ACCOUNT_TAG,     // https://ircv3.net/specs/extensions/account-tag
    ACCOUNT_NOTIFY,   // https://ircv3.net/specs/extensions/account-notify
    ECHO_MESSAGE,
    ZNC_SELF_MESSAGE,
    FISH              //
  };
}
