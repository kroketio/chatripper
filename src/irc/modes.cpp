#include "modes.h"

namespace irc {
  QMap<UserModes, UserModeInfo> userModesLookup = {};
  QMap<QChar, UserModes> userModesLookupLetter = {};

  void initializeUserModesLookup() {
    struct RawMode { UserModes mode; char letter; const char* desc; };
    constexpr RawMode rawModes[] = {
      {UserModes::INVISIBLE,                  'i', "invisible"},
      {UserModes::CLOAK,                      'x', "cloaks your IP/hostname"},
      {UserModes::BLOCK_PM_FROM_UNREGISTERED, 'R', "block private messages from unregistered users"},
      {UserModes::BEEP_BOOP_BOT,              'b', "mark as a bot"},
      {UserModes::DEAF,                       'd', "deaf (ignore channel messages)"},
      {UserModes::REGISTERED,                 'r', "registered user"},
      {UserModes::CALLER_ID,                  'g', "caller ID (only accept PMs from approved users)"},
      {UserModes::HIDE_CHANNELS,              'p', "hide channels in /WHOIS"},
      {UserModes::BLOCK_CTCP,                 'c', "block CTCP messages"},
      {UserModes::SECURE,                     'Z', "SSL/TLS connection"},
      {UserModes::IRC_OPERATOR,               'o', "IRC operator"},
      {UserModes::LOCAL_OPERATOR,             'O', "local IRC operator"},
      {UserModes::WALLOPS,                    'w', "receive wallops"},
      {UserModes::SERVER_NOTICES,             's', "receive server notices"},
      {UserModes::PROTECTED,                  'q', "protected/quiet"},
      {UserModes::SERVICE_BOT,                'k', "service bot"}
    };

    for (const auto&[mode, letter, desc] : rawModes) {
      userModesLookup.insert(mode, UserModeInfo{mode, letter, QString::fromUtf8(desc)});
      userModesLookupLetter.insert(QChar(letter), mode);
    }
  }
}