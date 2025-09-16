#pragma once

#include <QObject>
#include <QMap>

namespace irc {
  enum class ChannelModes : int {
    INVITE_ONLY       = 1 << 0,  // +i : Only invited users can join
    MODERATED         = 1 << 1,  // +m : Only voiced/ops may speak
    NO_OUTSIDE_MSGS   = 1 << 2,  // +n : No messages from outside channel
    QUIET             = 1 << 3,  // +q : Mutes user instead of kicking
    SECRET            = 1 << 4,  // +s : Channel is hidden from LIST
    TOPIC_PROTECTED   = 1 << 5,  // +t : Only ops can set the topic
    BAN               = 1 << 6,  // +b : Ban mask (usually a list mode)
    KEY               = 1 << 7,  // +k : Password required to join
    LIMIT             = 1 << 8,  // +l : User limit
    COUNT
  };

  enum class UserModes : int {
    INVISIBLE                  = 1 << 0,   // +i : Hides user from WHO/WHOIS unless in same channel
    CLOAK                      = 1 << 1,   // +x : Hides real hostname/IP, shows cloaked/virtual host
    BLOCK_PM_FROM_UNREGISTERED = 1 << 2,   // +R : Blocks private messages from unregistered users
    BEEP_BOOP_BOT              = 1 << 3,   // +B : Marks account as a bot
    DEAF                       = 1 << 4,   // +d : Ignores all channel messages (user is "deaf")
    REGISTERED                 = 1 << 5,   // +r : Indicates the user is identified/registered
    CALLER_ID                  = 1 << 6,   // +g : Blocks PMs unless user is on an accept list
    HIDE_CHANNELS              = 1 << 7,   // +p : Hides channel list in WHOIS output
    BLOCK_CTCP                 = 1 << 8,   // +T : Blocks CTCP (Client-To-Client Protocol) requests
    SECURE                     = 1 << 9,   // +z : Requires secure/SSL connection
    IRC_OPERATOR               = 1 << 10,  // +o : IRC operator (global privileges)
    LOCAL_OPERATOR             = 1 << 11,  // +O : Local IRC operator (limited to one server)
    WALLOPS                    = 1 << 12,  // +w : Receives WALLOPS messages (server-wide notices)
    SERVER_NOTICES             = 1 << 13,  // +s : Receives server notices
    PROTECTED                  = 1 << 14,  // +a : Marks as protected (cannot be kicked, usually for admins)
    SERVICE_BOT                = 1 << 15,  // +S : Service bot flag (marks a user as a service)
    COUNT                      = 16
  };

  struct UserModeInfo {
    UserModes mode;
    char letter;
    QString description;
  };

  struct ChannelModeInfo {
    ChannelModes mode;
    char letter;
    QString description;
  };

  // Lookups for user modes
  extern QMap<UserModes, UserModeInfo> userModesLookup;
  extern QMap<QChar, UserModes> userModesLookupLetter;
  void initializeUserModesLookup();

  // Lookups for channel modes
  extern QMap<ChannelModes, ChannelModeInfo> channelModesLookup;
  extern QMap<QChar, ChannelModes> channelModesLookupLetter;
  void initializeChannelModesLookup();
}