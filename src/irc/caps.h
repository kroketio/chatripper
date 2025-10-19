#pragma once

namespace irc {
  enum class PROTOCOL_CAPABILITY {
    MULTI_PREFIX      = 1 << 0,  // https://ircv3.net/specs/extensions/multi-prefix
    MESSAGE_TAGS      = 1 << 1,  // https://ircv3.net/specs/extensions/message-tags
    EXTENDED_JOIN     = 1 << 2,  //
    CHGHOST           = 1 << 3,  // https://ircv3.net/specs/extensions/chghost
    ACCOUNT_TAG       = 1 << 4,  // https://ircv3.net/specs/extensions/account-tag
    ACCOUNT_NOTIFY    = 1 << 5,  // https://ircv3.net/specs/extensions/account-notify
    ECHO_MESSAGE      = 1 << 6,
    ZNC_SELF_MESSAGE  = 1 << 7,
    FISH              = 1 << 8,
    CHANNEL_RENAME    = 1 << 9,  // https://ircv3.net/specs/extensions/channel-rename
    METADATA          = 1 << 10,
    FILEHOST          = 1 << 11,
    EXTENDED_ISUPPORT = 1 << 12
  };
}
