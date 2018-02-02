#include <cstdlib>
#include <string>
#include <getopt.h>
#include <libconfig.h++>
#include <iostream>
#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#if HAVE_PWD_H
#include <pwd.h>
#include <unistd.h>
#endif
#include "td/telegram/TdParameters.h"
#include "td/utils/FileLog.h"
#include "td/actor/actor.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/path.h"

#include "telegram.h"
#include "cliclient.hpp"
#include "lua.h"

#define CONFIG_DIRECTORY ".telegram-bot" 
#define CONFIG_DIRECTORY_MODE 0700

std::string config_filename;
std::string profile;
std::string logname;
std::string lua_script;
std::string phone;
std::string bot_hash;
std::string username;
std::string groupname;
std::string unix_socket;
std::string program;

bool login_mode;
bool daemonize;
bool accept_any_tcp_connections;

int verbosity;
int sfd = -1;
int usfd = -1;
int port = -1;

td::TdParameters param;

std::string get_home_directory () /* {{{ */ {
  auto str = getenv ("TELEGRAM_BOT_HOME");
  if (str && std::strlen (str)) {
    return str;
  }
  str = getenv ("HOME");
  if (str && std::strlen (str)) {
    return str;
  }
  
  #if HAVE_PWD_H
  setpwent ();
  auto user_id = getuid ();
  while (1) {
    auto current_passwd = getpwent ();
    if (!current_passwd) {
      break;
    }
    if (current_passwd->pw_uid == user_id) {
      endpwent ();
      return current_passwd->pw_dir;
    }
  }
  endpwent ();
  #endif
  
  return ".";
}
/* }}} */

std::string get_config_directory () /* {{{ */ {
  auto str = getenv ("TELEGRAM_CONFIG_DIR");
  if (str && std::strlen (str)) {
    return str;
  }
  
  // XDG: http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
  str = getenv ("XDG_CONFIG_HOME");
  if (str && std::strlen (str)) {
    return std::string (str) + "/" + CONFIG_DIRECTORY;
  }

  return get_home_directory () + "/" + CONFIG_DIRECTORY;
}
/* }}} */

void parse_config () /* {{{ */ {
  //config_filename = make_full_path (config_filename);
  /// Is test Telegram environment should be used instead of the production environment.
  param.use_test_dc = false;
  /// Application identifier for Telegram API access, can be obtained at https://my.telegram.org.
  param.api_id = TELEGRAM_CLI_API_ID;
  /// Application identifier hash for Telegram API access, can be obtained at https://my.telegram.org.
  param.api_hash = TELEGRAM_CLI_API_HASH;
  /// IETF language tag of users language.
  //param.language_code = "en";
  /// Model of a device application is run on.
  //param.device_model = "Console";
  /// Version of an operating system application is run on.
  //param.system_version = "UNIX/XX";
  /// Application version.
  //param.app_version = TELEGRAM_CLI_VERSION;
  /// If set to false, information about files will be lost on application restart.
  param.use_file_db = true;
  /// If set to true, old files will be automatically deleted.
  //param.use_file_gc = false;
  param.ignore_file_names = false;
  /// By default, secret chats support is disabled. Set to true to enable secret chats support.
  param.use_secret_chats = true;
  /// If set to true, the library will maintain cache of users, groups, channels and secret chats. Implies use_file_db.
  param.use_chat_info_db = true;
  /// If set to true, the library will maintain cache of chats and messages. Implies use_chat_info_db.
  param.use_message_db = true;

  bool default_config = false;
  if (config_filename.length () <= 0) {
    config_filename = "config";
    default_config = true;
  }
  if (config_filename[0] != '/') {
    config_filename = get_config_directory () + "/" + config_filename; 
  }

  if (default_config) {
    td::mkdir (get_config_directory ()).ensure ();
  }

  libconfig::Config conf;

  try {
    conf.readFile (config_filename.c_str ());
  } catch (const libconfig::FileIOException &e) {
    if (!default_config) {
      std::cerr << "can not open config file '" << config_filename << "'\n";
      std::cerr << "diagnosis: " << e.what () << "\n";
      
      exit (EXIT_FAILURE);
    } else {
      std::cout << "config '" << config_filename << "' not found. Using default config\n";
    }
  } catch (const libconfig::ParseException &e) {
    std::cerr << "can not parse config file at " << e.getFile () << ":" << std::to_string (e.getLine ()) << "\n";
    std::cerr << "diagnosis: " << e.getError () << "\n";

    exit (EXIT_FAILURE);
  }

  if (profile.length () == 0) {
    try {
      conf.lookupValue ("default_profile", profile);
    } catch (const libconfig::SettingNotFoundException &) {}
  }
  
  std::string prefix;
  if (profile.length () > 0) {
    prefix = profile + ".";
  } else {
    prefix = "";
  }
  
  std::string config_directory;
  if (profile.length () > 0) {
    config_directory = get_config_directory () + "/" + profile + "/";
  } else {
    config_directory = get_config_directory () + "/";
  }
    
  try {
    bool test_mode = false;
    conf.lookupValue (prefix + "test", test_mode);
    param.use_test_dc = test_mode;
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    std::string s;
    conf.lookupValue (prefix + "config_directory", s);
    if (s.length () > 0 && s[0] == '/') {
      config_directory = s + "/";
    } else if (s.length () > 0) {
      config_directory = get_config_directory () + "/" + s + "/";
    }
  } catch (const libconfig::SettingNotFoundException &) {}
  
  //try {
  //  conf.lookupValue (prefix + "language_code", param.language_code);
  //} catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "use_file_db", param.use_file_db);
  } catch (const libconfig::SettingNotFoundException &) {}
  
  //try {
  //  conf.lookupValue (prefix + "use_file_gc", param.use_file_gc);
  //} catch (const libconfig::SettingNotFoundException &) {}
  
  //try {
  //  conf.lookupValue (prefix + "file_readable_names", param.file_readable_names);
  //} catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "use_secret_chats", param.use_secret_chats);
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "use_chat_info_db", param.use_chat_info_db);
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "use_message_db", param.use_chat_info_db);
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "logname", logname);
    if (logname.length () > 0 && logname[0] != '/') {
      logname = config_directory + logname;
    }
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "verbosity", verbosity);
  } catch (const libconfig::SettingNotFoundException &) {}
  
  try {
    conf.lookupValue (prefix + "lua_script", lua_script);
    if (lua_script.length () > 0 && lua_script[0] != '/') {
      lua_script = config_directory + lua_script;
    }
  } catch (const libconfig::SettingNotFoundException &) {}

  std::cout << config_directory << "\n";
  param.database_directory = config_directory + "/data";
  param.files_directory = config_directory + "/files";
 
  td::mkdir (config_directory, CONFIG_DIRECTORY_MODE).ensure ();
}
/* }}} */

void usage () /* {{{ */ {
  std::cout 
  << "Usage: \n"
  << "  --verbosity/-v                       increase verbosity (0-ERROR 1-WARNIN 2-NOTICE 3+-DEBUG-levels)\n"
  << "  --config/-c                          config file name\n"
  << "  --profile/-p                         use specified profile\n"
  << "  --daemonize/-d                       daemon mode\n"
  << "  --logname/-L <log-name>              log file name\n"
  << "  --username/-U <user-name>            change uid after start\n"
  << "  --groupname/-G <group-name>          change gid after start\n"
  << "  --tcp-port/-P <port>                 port to listen for input commands\n"
  << "  --unix-socket/-S <socket-name>        unix socket to create\n"
  << "  --exec/-e <commands>                 make commands end exit\n"
  << "  --help/-h                            prints this help\n"
  << "  --accept-any-tcp                     accepts tcp connections from any src (only loopback by default)\n"
  << "  --bot/-b <hash>                      bot mode\n" 
  << "  --phone/-u <phone>                   specify username (would not be asked during authorization)\n"
  << "  --login                              start in login mode\n"
  ;

  exit (1);
}
/* }}} */ 

void args_parse (int argc, char *argv[]) {
  static struct option long_options[] = {
    {"verbosity", no_argument, 0, 'v'},
    {"config", required_argument, 0, 'c'},
    {"profile", required_argument, 0, 'p'},
    {"lua-script", required_argument, 0, 's'},
    {"daemonize", no_argument, 0, 'd'},
    {"logname", required_argument, 0, 'L'},
    {"username", required_argument, 0, 'U'},
    {"groupname", required_argument, 0, 'G'},
    {"tcp-port", required_argument, 0, 'P'},
    {"unix-socket", required_argument, 0, 'S'},
    {"exec", required_argument, 0, 'e'},
    {"help", no_argument, 0, 'h'},
    {"bot", required_argument, 0, 'b'},
    {"phone", required_argument, 0, 'u'},
    {"accept-any-tcp", no_argument, 0,  1001},
    {"login", no_argument, 0,  1002},
    {0,         0,                 0,  0 }
  };


  int opt = 0;
  while ((opt = getopt_long (argc, argv, "vc:p:s:dL:U:G:P:S:e:hb:u:"
  , long_options, NULL
  )) != -1) {
    switch (opt) {
    case 'v':
      verbosity ++;
      break;
    case 'c':
      config_filename = optarg;
      break;
    case 'p':
      profile = optarg;
      break;
    case 's':
      lua_script = optarg;
      break;
    case 'd':
      daemonize = true;
      break;
    case 'L':
      logname = optarg;
      break;
    case 'U':
      username = optarg;
      break;
    case 'G':
      groupname = optarg;
      break;
    case 'P':
      port = atoi (optarg);
      break;
    case 'S':
      unix_socket = optarg;
      break;
    case 'e':
      program = optarg;
      break;
    case 'h':
      usage ();
      break;
    case 'b':
      if (bot_hash.length () > 0 || phone.length () > 0) {
        std::cout << "should have at most one option of --bot and --phone\n";
        usage ();
      }
      bot_hash = optarg;
      break;
    case 'u':
      if (bot_hash.length () > 0 || phone.length () > 0) {
        std::cout << "should have at most one option of --bot and --phone\n";
        usage ();
      }
      phone = optarg;
      break;
    case 1001:
      accept_any_tcp_connections = true;
      break;
    case 1002:
      login_mode = true;
      break;
    default:
      usage ();
      break;
    }
  }
}

void termination_signal_handler (int signum) {
  td::signal_safe_write_signal_number (signum);

  #if HAVE_EXECINFO_H
  void *buffer[255];
  const int calls = backtrace (buffer, sizeof (buffer) / sizeof (void *));
  backtrace_symbols_fd (buffer, calls, 1);
  #endif 
  
  exit (EXIT_FAILURE);
}

void main_loop() {
  if (logname.length () > 0) {
    static td::FileLog file_log;
    file_log.init (logname);
    td::log_interface = &file_log;
  }

  td::ConcurrentScheduler scheduler;
  scheduler.init(4);

  scheduler.create_actor_unsafe<CliClient>(0, "CliClient", port, accept_any_tcp_connections ? "0.0.0.0" : "127.0.0.1", lua_script, login_mode, phone, bot_hash, param).release();

  scheduler.start();
  while (scheduler.run_main(100)) {
  }
  scheduler.finish();
}

int main (int argc, char *argv[]) {
  td::setup_signals_alt_stack ().ensure ();
  td::set_signal_handler (td::SignalType::Abort, termination_signal_handler).ensure ();
  td::set_signal_handler (td::SignalType::Error, termination_signal_handler).ensure ();
  td::set_signal_handler (td::SignalType::Quit, termination_signal_handler).ensure ();
  td::ignore_signal (td::SignalType::Pipe).ensure ();
  
  args_parse (argc, argv);
  parse_config ();

  if (login_mode) {
    if (phone.length () <= 0 && bot_hash.length () <= 0) {
      std::cout << "in login mode need exactly one of phone and bot_hash\n";
      usage ();
    }
  }
  
  td::FileLog file_log;
  if (logname.length () > 0) {
    file_log.init(logname);
    td::log_interface = &file_log;
  }
      
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + verbosity);
  
  main_loop ();

  return 0;
}
