#include <assert.h>

#include "clilua.hpp"
#include "json.hpp"

CliLua *CliLua::instance_ = nullptr;

using json = nlohmann::json;

void parse_json (lua_State *L, json &res) {
  if (lua_isboolean (L, -1)) {
    res = static_cast<bool>(lua_toboolean (L, -1));
    return;
  } else if (lua_type (L, -1) == LUA_TNUMBER) {
    auto x = lua_tonumber (L, -1);
    auto x64 = static_cast<td::int64>(x);
    if (x == static_cast<double>(x64)) {
      res = x64;
    } else {
      res = x;
    }
    return;
  } else if (lua_isstring (L, -1)) {
    size_t len;
    const char *s = lua_tolstring (L, -1, &len);
    res = std::string (s, len);
    return;
  } else if (lua_istable (L, -1)) {
    
    bool arr = true;
    int size = 0;

    lua_pushnil (L);
    while (lua_next (L, -2)) {
      if (!(arr && lua_type (L, -2) == LUA_TNUMBER && lua_tointeger (L, -2) == size)) {
        arr = false;
      }
      size ++;
      lua_pop (L, 1);
    }

    if (arr) {
      res = json::array ();
    } else {
      res = json::object ();
    }
   
    lua_pushnil (L);
    while (lua_next (L, -2)) {
      if (arr) {
        int x = (int)lua_tointeger (L, -2);
        parse_json (L, res[x]);
        lua_pop (L, 1);
      } else {
        size_t len;

        if (lua_type (L, -2) == LUA_TNUMBER) {
          auto x = lua_tointeger (L, -2);
          std::string k = std::to_string (x);
          parse_json (L, res[k]);
        } else {
          const char *key = lua_tolstring (L, -2, &len);
          std::string k = std::string (key, len);
          parse_json (L, res[k]);
        }
        lua_pop (L, 1);
      }
    }
  } else {
    res = false;
    return;
  }
}

void push_json (lua_State *L, json &j) {
  if (j.is_null ()) {
    lua_pushnil (L);
  } else if (j.is_boolean ()) {
    lua_pushboolean (L, j.get<bool>());
  } else if (j.is_string ()) {
    auto s = j.get<std::string>();
    lua_pushlstring (L, s.c_str (), s.length ());
  } else if (j.is_number_integer ()) {
    auto v = j.get<td::int64>();

    if (v == static_cast<int>(v)) {
      lua_pushnumber (L, static_cast<int>(v));
    } else {
      std::string s = std::to_string (v);
      lua_pushlstring (L, s.c_str (), s.length ());
    }
  } else if (j.is_number_float ()) {
    auto v = j.get<double>();
    lua_pushnumber (L, v);
  } else if (j.is_array ()) {
    lua_newtable (L);
    int p = 0;
    for (auto it = j.begin (); it != j.end (); it ++, p ++) {
      lua_pushnumber (L, p);
      push_json (L, *it);
      lua_settable (L, -3);
    }
  } else if (j.is_object ()) {
    lua_newtable (L);
    for (auto it = j.begin (); it != j.end (); it ++) {
      auto s = it.key ();

      lua_pushlstring (L, s.c_str (), s.length ());
      push_json (L, it.value ());
      lua_settable (L, -3);
    }
  } else {
    lua_pushnil (L);
  }
}

int lua_parse_function (lua_State *L) {
  if (lua_gettop (L) != 3) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  int a1 = luaL_ref (L, LUA_REGISTRYINDEX);
  int a2 = luaL_ref (L, LUA_REGISTRYINDEX);
  
  json j;
  parse_json (L, j);
  lua_pop (L, 1);

  auto cmd = j.dump ();
  
  LOG(INFO) << cmd << "\n";
  
  auto res = td::json_decode (cmd);

  if (res.is_ok ()) {
    auto as_json_value = res.move_as_ok ();
    td::tl_object_ptr<td::td_api::Function> object;

    auto r = from_json(object, as_json_value);
  

    if (r.is_ok ()) {
      CliClient::instance_->send_request(std::move (object), std::make_unique<TdLuaCallback>(a1, a2, CliLua::instance_));
      lua_pushboolean (L, 1);
      return 1;
    } else {
      LOG(ERROR) << "FAILED TO PARSE LUA: " << r.move_as_error () << "\n";
      lua_pushboolean (L, 0);
      return 1;
    }
  }
  
  LOG(ERROR) << "FAILED TO PARSE LUA: " << res.move_as_error () << "\n";

  lua_pushboolean (L, 0);
  return 1;
}

CliLua::CliLua (std::string file) {
  instance_ = this;
  
  luaState_ = luaL_newstate ();
  luaL_openlibs (luaState_);
  
  lua_register (luaState_, "tdbot_function", lua_parse_function);

  int r = luaL_dofile (luaState_, file.c_str ());

  if (r) {
    LOG(FATAL) << "lua: " << lua_tostring (luaState_, -1) << "\n";
  }
}

void CliLua::update (std::string update) {
  auto j = json::parse (update);

  lua_settop (luaState_, 0);
  lua_getglobal (luaState_, "tdbot_update_callback");

  push_json (luaState_, j);
  
  int r = lua_pcall (luaState_, 1, 0, 0);

  if (r) {
    LOG(FATAL) << "lua: " <<  lua_tostring (luaState_, -1) << "\n";
  }

}
  
void TdLuaCallback::on_result (td::tl_object_ptr<td::td_api::Object> result) {
  std::string v = td::json_encode<std::string>(td::ToJson (result));

  clua_->result (v, a1_, a2_);
}

void CliLua::result (std::string update, int a1, int a2) {
  auto j = json::parse (update);

  lua_settop (luaState_, 0);

  lua_rawgeti (luaState_, LUA_REGISTRYINDEX, a2);
  lua_rawgeti (luaState_, LUA_REGISTRYINDEX, a1);
 
  push_json (luaState_, j);

  int r = lua_pcall (luaState_, 2, 0, 0);

  luaL_unref (luaState_, LUA_REGISTRYINDEX, a1);
  luaL_unref (luaState_, LUA_REGISTRYINDEX, a2);

  if (r) {
    LOG(FATAL) << "lua: " <<  lua_tostring (luaState_, -1) << "\n";
  }
}
  
