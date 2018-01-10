#pragma once

#include "cliclient.hpp"

#include <lua.hpp>
#include <string>

class CliClient;

class CliLua {
  public:
    CliLua (std::string file);
    void update(std::string upd);
    void result(std::string result, int a1, int a2);
    static CliLua *instance_;
  private:
    lua_State *luaState_;
};

class TdLuaCallback : public TdQueryCallback {
  void on_result (td::tl_object_ptr<td::td_api::Object> result) override;
  void on_error (td::tl_object_ptr<td::td_api::error> error) override {
    on_result (td::move_tl_object_as<td::td_api::Object> (error));
  }

  int a1_, a2_;
  CliLua *clua_;
  
  public:
  TdLuaCallback(int a1, int a2, CliLua *clua) : a1_(a1), a2_(a2), clua_ (clua) {
  }

};
