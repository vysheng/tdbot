#pragma once

#include <list>
#include <set>
#include <unordered_map>
#include <map>
#include <string>
#include <sstream>

#include "td/telegram/ClientActor.h"
#include "td/actor/actor.h"
#include "td/tl/TlObject.h"
#include "td/utils/port/ServerSocketFd.h"
#include "td/utils/Container.h"
#include "td/telegram/TdParameters.h"

#include "auto/td/telegram/td_api.h"
#include "auto/td/telegram/td_api.hpp"

#include "td/tl/tl_json.h"
#include "auto/td/telegram/td_api_json.h"


class CliLua;

class TdQueryCallback {
  public:
    virtual void on_result(td::tl_object_ptr<td::td_api::Object> result) = 0;    
    virtual void on_error(td::tl_object_ptr<td::td_api::error> error) = 0;
    virtual ~TdQueryCallback() {
    }
};

class CliClient;

class CliFd {
  public:
    CliFd() {}
    void work(td::uint64 id);
    virtual void write(std::string str) = 0;
  private:
    virtual void sock_read (td::uint64 id) = 0;
    virtual void sock_write (td::uint64 id) = 0;
    virtual void sock_close (td::uint64 id) = 0;
};

class CliStdFd : public CliFd {
  public:
    CliStdFd (td::Fd stdin_, td::Fd stdout_, CliClient *cli_);
    void write(std::string str) override {
      out_ += str + "\n";
    }
  private:
    void sock_read (td::uint64 id) override;
    void sock_write (td::uint64 id) override;
    void sock_close (td::uint64 id) override;
    td::Fd stdin_;
    td::Fd stdout_;
    CliClient *cli_;
    std::string in_;
    std::string out_;
    bool half_closed_ = false;
};

class CliSockFd : public CliFd {
  public:
    CliSockFd (td::SocketFd fd_, CliClient *cli_);
    void write(std::string str) override {
      out_ += str + "\n";
    }
  private:
    void sock_read (td::uint64 id) override;
    void sock_write (td::uint64 id) override;
    void sock_close (td::uint64 id) override;
    td::SocketFd fd_;
    CliClient *cli_;
    std::string in_;
    std::string out_;
};

class CliClient final : public td::Actor {
 public:
  explicit CliClient(int port, std::string addr, std::string lua_script, bool login_mode, std::string phone, std::string bot_hash, td::TdParameters param) : port_(port), addr_(addr), lua_script_(lua_script), login_mode_ (login_mode), phone_ (phone), bot_hash_ (bot_hash), param_(param) {
  }

  class TdAuthorizationStateCallback : public TdQueryCallback {
    void on_result (td::tl_object_ptr<td::td_api::Object> result) override {
      CHECK (result->get_id () == td::td_api::ok::ID);
    }
    void on_error (td::tl_object_ptr<td::td_api::error> error) override {
      instance_->authentificate_restart ();
    }
  };

  class TdCmdCallback : public TdQueryCallback {
    void on_result (td::tl_object_ptr<td::td_api::Object> result) override {
      auto T = cli_->fds_.get (id_);
      if (T) {
        std::string v = td::json_encode<std::string>(td::ToJson (result));
        T->get ()->write (v);
        T->get ()->work (id_);
      }
    }
    void on_error (td::tl_object_ptr<td::td_api::error> error) override {
      on_result (td::move_tl_object_as<td::td_api::Object> (error));
    }

    td::uint64 id_;
    CliClient *cli_;
    
    public:
    TdCmdCallback(td::uint64 id, CliClient *cli) : id_ (id), cli_ (cli) {
    }

  };

  
  void send_request(td::tl_object_ptr<td::td_api::Function> f, std::unique_ptr<TdQueryCallback> handler) {
    auto id = handlers_.create(std::move(handler));
    if (!td_.empty()) {
      send_closure(td_, &td::ClientActor::request, id, std::move(f));
    } else {
      LOG(ERROR) << "Failed to send: " << td::td_api::to_string(f);
    }
  };
  
  static CliClient *instance_;

  void del_fd (td::uint64 id) {
    fds_.erase (id);
  }

  void run (td::uint64 id, std::string cmd) {
    while (cmd.length () > 0 && isspace (cmd[0])) {
      cmd = cmd.substr (1);
    }
    while (cmd.length () > 0 && isspace (cmd[cmd.length () - 1])) {
      cmd = cmd.substr (0, cmd.length () - 1);
    }
    auto res = td::json_decode (cmd);
   
    if (res.is_ok ()) {
      auto as_json_value = res.move_as_ok ();
      td::tl_object_ptr<td::td_api::Function> object;
      
      auto r = from_json(object, as_json_value);
      
      if (r.is_ok ()) {
        send_request(std::move (object), std::make_unique<TdCmdCallback>(id,this));
        return;
      } else {
        auto R = r.move_as_error ();

        std::string er = std::string ("") +  "{\"_\":\"error\",\"code\":" + std::to_string (R.code ()) + ",\"message\":\"" + R.public_message () + "\"}";

        if (fds_.get (id)) {
          fds_.get (id)->get ()->write (er);
        }
        return;
      }
    }


    auto R = res.move_as_error ();

    std::string er = std::string ("") +  "{\"_\":\"error\",\"code\":" + std::to_string (R.code ()) + ",\"message\":\"" + R.public_message () + "\"}";
    
    if (fds_.get (id)) {
      fds_.get (id)->get ()->write (er);
    }
  }

 private:
  void authentificate_restart ();
  void authentificate_continue (td::td_api::AuthorizationState &state);
  void login_continue (const td::td_api::authorizationStateReady &result);
  void login_continue (const td::td_api::authorizationStateWaitTdlibParameters &result);
  void login_continue (const td::td_api::authorizationStateWaitPhoneNumber &result);
  void login_continue (const td::td_api::authorizationStateWaitCode &result);
  void login_continue (const td::td_api::authorizationStateWaitPassword &result);
  void login_continue (const td::td_api::authorizationStateLoggingOut &result);
  void login_continue (const td::td_api::authorizationStateWaitEncryptionKey &result);
  void login_continue (const td::td_api::authorizationStateClosing &result);
  void login_continue (const td::td_api::authorizationStateClosed &result);

  void start_up() override {
    yield();
  }
  
  void on_update (td::tl_object_ptr<td::td_api::Update> update);
  void on_result (td::uint64 id, td::tl_object_ptr<td::td_api::Object> result);
  void on_error (td::uint64 id, td::tl_object_ptr<td::td_api::error> error);

  void on_before_close() {
    td_.reset();
  }

  void on_closed() {
    LOG(INFO) << "on_closed";
    if (close_flag_) {
      ready_to_stop_ = true;
      yield();
      return;
    }
    init_td();
  }
  

  std::unique_ptr<td::TdCallback> make_td_callback();
  
  void init_td() {
    td_ = td::create_actor<td::ClientActor>(td::Slice ("TDPROXY"), make_td_callback());
  }

  void init ();


  bool inited_ = false;
  void loop() override;


  void timeout_expired() override {
  }

  /*void add_cmd(std::string cmd) {
    cmd_queue_.push(cmd);
  }*/

  //td::int32 my_id_ = 0;

  int port_;
  std::string addr_;
  CliLua *clua_ = nullptr;
  std::string lua_script_ = "";
  bool login_mode_ = false;
  std::string phone_;
  std::string bot_hash_;

  td::TdParameters param_;
  td::ActorOwn<td::ClientActor> td_;
  //std::queue<std::string> cmd_queue_;
  bool close_flag_ = false;
  bool ready_to_stop_ = false;
  td::ServerSocketFd listen_;

  td::Container<std::unique_ptr<CliFd>> fds_;
  td::Container<std::unique_ptr<TdQueryCallback>> handlers_;
};
