#include <cstddef>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <iostream>
#include <string>
#include <cwctype>
#include <ctime>

#ifdef WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "telegram.h"
#include "cliclient.hpp"
#include "clilua.hpp"

#include "td/utils/port/StdStreams.h"
#include "td/utils/Slice.h"

void set_stdin_echo (bool enable) {
#ifdef WIN32
  HANDLE hStdin = GetStdHandle (STD_INPUT_HANDLE); 
  DWORD mode;
  GetConsoleMode (hStdin, &mode);

  if (!enable) {
    mode &= ~ENABLE_ECHO_INPUT;
  } else {
    mode |= ENABLE_ECHO_INPUT;
  }

  SetConsoleMode (hStdin, mode);
#else
  struct termios tty;
  tcgetattr (STDIN_FILENO, &tty);
  if (!enable) {
    tty.c_lflag &= ~ECHO;
  } else {
    tty.c_lflag |= ECHO;
  }

  (void) tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}


CliClient *CliClient::instance_ = nullptr;

CliSockFd::CliSockFd(td::SocketFd fd, CliClient *cli) : fd_ (std::move (fd)), cli_ (cli) {
  fd_.get_native_fd ().set_is_blocking (false).ensure ();
  td::Scheduler::subscribe(fd_.get_poll_info ().extract_pollable_fd (cli_), td::PollFlags::ReadWrite() | td::PollFlags::Close() | td::PollFlags::Error());
}

CliSockFd::~CliSockFd() {
  close ();
}

CliStdFd::CliStdFd(CliClient *cli) : cli_ (cli) {
  td::Stdin().get_native_fd ().set_is_blocking (false).ensure ();
  td::Scheduler::subscribe(td::Stdin ().get_poll_info ().extract_pollable_fd (cli_), td::PollFlags::ReadWrite() | td::PollFlags::Close() | td::PollFlags::Error());
  td::Stdout().get_native_fd ().set_is_blocking (false).ensure ();
  td::Scheduler::subscribe(td::Stdout ().get_poll_info ().extract_pollable_fd (cli_), td::PollFlags::Write() | td::PollFlags::Close() | td::PollFlags::Error());
}

CliStdFd::~CliStdFd() {
  td::Scheduler::unsubscribe(td::Stdin ().get_poll_info ().get_pollable_fd_ref ());
  td::Scheduler::unsubscribe(td::Stdout ().get_poll_info ().get_pollable_fd_ref ());
}

void CliFd::work (td::uint64 id) {
  sock_read (id);
  sock_write (id);
  sock_close (id);
}

void CliSockFd::sock_read (td::uint64 id) {
  while (td::can_read (fd_)) {
    char sb[1024];
    td::MutableSlice s(sb, 1024);
    auto res = fd_.read (s);

    if (res.is_ok ()) {
      in_ += s.substr (0, res.ok ()).str ();
    }
  }
  
  if (in_.length () > 0) {
    while (1) {
      auto p = in_.find ('\n');
      if (p >= in_.length ()) {
        break;
      }
      auto s = in_.substr (0, p);
      if (s.length () > 0) {
        cli_->run (id, s);
      }
      in_.erase (0, p + 1);
    }
  }
}

void CliStdFd::sock_read (td::uint64 id) {
  while (!half_closed_ && td::can_read (td::Stdin())) {
    char sb[1024];
    td::MutableSlice s(sb, 1024);
    auto res = td::Stdin().read (s);

    if (res.is_ok ()) {
      in_ += s.substr (0, res.ok ()).str ();
    }
  }
  
  if (in_.length () > 0) {
    while (1) {
      auto p = in_.find ('\n');
      if (p >= in_.length ()) {
        break;
      }
      auto s = in_.substr (0, p);
      in_.erase (0, p + 1);
      if (s.length () > 0) {
        cli_->run (id, s);
      }
    }
  }
}

void CliSockFd::sock_write (td::uint64 id) {
  while (td::can_write (fd_) && out_.length () > 0) {
    td::Slice s(out_);
    auto res = fd_.write (s);

    if (res.is_ok ()) {
      out_.erase (0, res.ok ());
    }
  }
}

void CliStdFd::sock_write (td::uint64 id) {
  while (td::can_write (td::Stdout()) && out_.length () > 0) {
    td::Slice s(out_);
    auto res = td::Stdout().write (s);

    if (res.is_ok ()) {
      out_.erase (0, res.ok ());
    }
  }
}

void CliSockFd::sock_close (td::uint64 id) {
  if (td::can_close (fd_)) {
    close ();
    cli_->del_fd (id);
  }
}

void CliSockFd::close () {
  if (!fd_.empty()) {
    td::Scheduler::unsubscribe(fd_.get_poll_info ().get_pollable_fd_ref ());
    fd_.close ();
  }
}


void CliStdFd::sock_close (td::uint64 id) {
  if (td::can_close (td::Stdin())) {
    half_closed_ = true;
  }
  if (td::can_close (td::Stdout())) {
    half_closed_ = true;
    cli_->del_fd (id);
  }
}

void CliClient::authentificate_restart () {
  //send_request (td::make_tl_object<td::td_api::getAuthorizationState>(), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::login_continue (const td::td_api::authorizationStateReady &result) {
  std::cout << "logged in successfully\n";

  if (login_mode_) {
    td_.reset();
    close_flag_ = true;
  }
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitPhoneNumber &result) {
  if (!login_mode_) {
    LOG(FATAL) << "not logged in. Try running with --login option";
  }
  if (phone_.length () > 0) {
    send_request (td::make_tl_object<td::td_api::setAuthenticationPhoneNumber>(phone_, nullptr), std::make_unique<TdAuthorizationStateCallback>());
  } else {
    send_request (td::make_tl_object<td::td_api::checkAuthenticationBotToken>(bot_hash_), std::make_unique<TdAuthorizationStateCallback>());
  }
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitOtherDeviceConfirmation &result) {
  LOG(FATAL) << "unexpected authorization state";
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitCode &R) {
  if (!login_mode_) {
    LOG(FATAL) << "not logged in. Try running with --login option";
  }

  std::cout << "code: ";
  set_stdin_echo (false);
  std::string code;
  std::getline (std::cin, code);
  set_stdin_echo (true);
  std::cout << "\n";

  send_request (td::make_tl_object<td::td_api::checkAuthenticationCode>(code), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitRegistration &R) {
  if (!login_mode_) {
    LOG(FATAL) << "not logged in. Try running with --login option";
  }

  std::string first_name;
  std::string last_name;

  std::cout << "not registered\n";
  std::cout << "first name: ";
  std::getline (std::cin, first_name);
  std::cout << "last name: ";
  std::getline (std::cin, last_name);

  send_request (td::make_tl_object<td::td_api::registerUser>(first_name, last_name), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitPassword &result) {
  if (!login_mode_) {
    LOG(FATAL) << "not logged in. Try running with --login option";
  }
  std::cout << "password: ";
  set_stdin_echo (false);
  std::string password;
  std::getline (std::cin, password);
  set_stdin_echo (true);
  std::cout << "\n";

  send_request (td::make_tl_object<td::td_api::checkAuthenticationPassword>(password), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::login_continue (const td::td_api::authorizationStateLoggingOut &result) {
  std::cout << "logging out\n";
}

void CliClient::login_continue (const td::td_api::authorizationStateClosing &result) {
  std::cout << "closing\n";
}

void CliClient::login_continue (const td::td_api::authorizationStateClosed &result) {
  std::cout << "closed\n";
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitTdlibParameters &result) {
//tdlibParameters use_test_dc:Bool database_directory:string files_directory:string use_file_database:Bool use_chat_info_database:Bool use_message_database:Bool use_secret_chats:Bool api_id:int32 api_hash:string system_language_code:string device_model:string system_version:string application_version:string enable_storage_optimizer:Bool ignore_file_names:Bool = TdlibParameters;
  send_request (td::make_tl_object<td::td_api::setTdlibParameters>(td::make_tl_object<td::td_api::tdlibParameters>(param_.use_test_dc, param_.database_directory, param_.files_directory, true, true, true, true, param_.api_id, param_.api_hash, "en", "Unix/Console/Bot", "UNIX/??", TELEGRAM_CLI_VERSION, false, param_.ignore_file_names)), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::login_continue (const td::td_api::authorizationStateWaitEncryptionKey &result) {
  send_request (td::make_tl_object<td::td_api::checkDatabaseEncryptionKey>(""), std::make_unique<TdAuthorizationStateCallback>());
}

void CliClient::authentificate_continue (td::td_api::AuthorizationState &result) {
  downcast_call (result, [&](auto &object){this->login_continue (object);});
}

void CliClient::on_update (td::tl_object_ptr<td::td_api::Update> update) {
  if (update->get_id () == td::td_api::updateAuthorizationState::ID) {
    auto t = td::move_tl_object_as<td::td_api::updateAuthorizationState>(update);
    authentificate_continue (*t->authorization_state_);
    update = td::move_tl_object_as<td::td_api::Update>(t);

  }
  auto object = td::td_api::move_object_as<td::td_api::Object>(update);
  std::string v = td::json_encode<std::string>(td::ToJson (object));

  fds_.for_each ([&](td::uint64 id, auto &x) {  
    x.get()->write (v);
    x.get()->work (id);
    });

  if (clua_) {
    clua_->update (v); 
  }
}
void CliClient::on_result (td::uint64 id, td::tl_object_ptr<td::td_api::Object> result) {
  if (id == 0) {
    on_update (td::move_tl_object_as<td::td_api::Update>(result));
    return;
  }   

  auto *handler_ptr = handlers_.get(id);
  CHECK(handler_ptr != nullptr);
  auto handler = std::move(*handler_ptr);
  handler->on_result(std::move(result));
  handlers_.erase(id);
}
void CliClient::on_error (td::uint64 id, td::tl_object_ptr<td::td_api::error> error) {
  auto *handler_ptr = handlers_.get(id);
  CHECK(handler_ptr != nullptr);
  auto handler = std::move(*handler_ptr);
  handler->on_error(std::move (error));
  handlers_.erase(id);
}

void CliClient::loop() {
  if (!inited_) {
    inited_ = true;
    init();
  }

  if (port_ > 0) {
    while (td::can_read (listen_)) {
      auto r = listen_.accept ();
      if (r.is_ok ()) {
        auto x = std::make_unique<CliSockFd>(r.move_as_ok (), this);
        fds_.create (std::move (x));
        LOG(INFO) << "accepted connection\n";
      }
    }
    if (td::can_close (listen_)) {
      LOG(FATAL) << "listening socket unexpectedly closed\n";
    }
  }

  fds_.for_each ([&](td::uint64 id, auto &x) {  
    x.get()->work (id);
    });
    
  if (ready_to_stop_) {
    td::Scheduler::instance()->finish();
    stop();
  }
}

td::unique_ptr<td::TdCallback> CliClient::make_td_callback() {
  class TdCallbackImpl : public td::TdCallback {
    public:
      explicit TdCallbackImpl(CliClient *client) : client_(client) {
      }
      void on_result(td::uint64 id, td::tl_object_ptr<td::td_api::Object> result) override {
        client_->on_result(id, std::move(result));
      }
      void on_error(td::uint64 id, td::tl_object_ptr<td::td_api::error> error) override {
        client_->on_error(id, std::move(error));
      }
      ~TdCallbackImpl() override {
        client_->on_closed ();
      }

    private:
      CliClient *client_;
  };
  return td::make_unique<TdCallbackImpl>(this);
}

void CliClient::init() {
  instance_ = this;
  init_td();

  if (!login_mode_) {
    auto x = std::make_unique<CliStdFd>(this);
    fds_.create (std::move (x));

    if (port_ > 0) {
      auto r = td::ServerSocketFd::open (port_, addr_);
      if (r.is_ok ()) {
        listen_ = r.move_as_ok ();
        td::Scheduler::subscribe(listen_.get_poll_info ().extract_pollable_fd (this), td::PollFlags::ReadWrite() | td::PollFlags::Close() | td::PollFlags::Error());
      } else {
        LOG(FATAL) << "can not initialize listening socket on port " << port_;
      }
    }

    if (lua_script_.length () > 0) {
      clua_ = new CliLua (lua_script_);
    }
  }

  authentificate_restart (); 
}

void CliClient::tear_down() {
  if (!listen_.empty()) {
    td::Scheduler::unsubscribe(listen_.get_poll_info ().get_pollable_fd_ref ());
  }
}
