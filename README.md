## telegram-bot

A userbot interface for [Telegram](http://telegram.org). Uses [TDLib](https://github.com/tdlib/td).

### API, Protocol documentation

- [Telegram API](http://core.telegram.org/api)
- [MTproto protocol](http://core.telegram.org/mtproto)
- [TDLib](https://core.telegram.org/tdlib)

### Build Instruction

#### Linux and BSDs

Install utils and libs: git, cmake, libssl, liblua, liblua, gperf, libconfig.

On Ubuntu/Debian use:

```sh
sudo apt install git build-essential cmake libssl-dev liblua5.2-dev gperf libconfig++-dev
```

1.  Clone GitHub Repository

    ```sh
    git clone --recursive https://github.com/vysheng/tdbot.git
    ```

2.  Create build folder

    ```sh
    mkdir tdbot-build
    ```

3.  Enter build folder

    ```sh
    cd tdbot-build
    ```

4.  Configure

    ```sh
    cmake -DCMAKE_BUILD_TYPE=Release ../tdbot
    ```

5.  Start build process

    ```sh
    make telegram-bot
    ```

### Usage

1.  First of all you need to create config. Default config name is `config` which is located in `${HOME}/.telegram-bot/` or `${HOME}/.config/telegram-bot/`.
2.  Then you need to login. If you want to login as bot you need to run:

    ```sh
    telegram-bot -p profile-name --login --bot=bot-token
    ```

    And, if you want to login as user you need to run:

    ```sh
    telegram-bot -p profile-name --login --phone=phone-number
    ```

3.  Now you can run commands. There are two ways to do it, you can use: (1) json interface (via stdin or tcp connections), or (2) lua script.\
To understand how to serialize commands you need to read examples and tl scheme.

### examples

1.  Json response

    ```json
    {"_":"sendMessage", "chat_id":1007779878, "reply_to_message_id":0, "disable_notification":0, "from_background":0, "input_message_content":{"_":"inputMessageText", "text":"Test text here", "disable_web_preview":0, "clear_draft":0, "entities":[]}}
    ```

1.  Configuration file

    ```
    # This is an empty config file
    # Feel free to put something here

    default_profile = "main";

    main =  {
      # connect to totally separate telegram environment
      # it is used only for tests
      # false is default value
      test = false;

      # folder containing data for this profile
      # default value is profile name
      config_directory = "main";

      # language code. Some telegram notifications
      # may use it. Default is "en"
      language_code = "en";

      # use file db. Allows files reuse after restart
      # default value is true
      use_file_db = true;

      # use file garbage collector. Deletes files unused for 30 days
      # default value is true
      use_file_gc = true;

      # use file names as specified in document description
      # instead telegram-bot can use random names
      # default value is true
      file_readable_names = true;

      # allow accepting and creating secret chats
      # default value is true
      use_secret_chats = true;

      # use chat info db. Allow to send messages to chats instantly after restart
      # default value is true
      use_chat_info_db = true;

      # use message db
      # default value is true
      use_message_db = true;

      # logname. if not starts with '/' is relative to config_directory
      # if empty log to stderr
      # default value is empty
      logname = "log.txt";

      # log verbosity. Default value is 0
      verbosity = 2;

      # LUA script to use. If empty do not use lua. Relative to config_directory
      # default value is empty
      lua_script = "script.lua";
    };

    test_dc1 = {
      test = true;
      verbosity = 100;
      logname = "log.txt";
    };

    # in many cases default values are OK, so config is empty
    second = {
    };

    ```

1.  Lua script

    ```lua
    function vardump(value, depth, key)
      local linePrefix = ""
      local spaces = ""

      if key ~= nil then
        linePrefix = "["..key.."] = "
      end

      if depth == nil then
        depth = 0
      else
        depth = depth + 1
        for i=1, depth do spaces = spaces .. "  " end
      end

      if type(value) == 'table' then
        mTable = getmetatable(value)
      if mTable == nil then
        print(spaces ..linePrefix.."(table) ")
      else
        print(spaces .."(metatable) ")
        value = mTable
      end
      for tableKey, tableValue in pairs(value) do
        vardump(tableValue, depth, tableKey)
      end
      elseif type(value)  == 'function' or
        type(value) == 'thread' or
        type(value) == 'userdata' or
        value   == nil
      then
        print(spaces..tostring(value))
      else
        print(spaces..linePrefix.."("..type(value)..") "..tostring(value))
      end
    end


    function dl_cb (arg, data)
      vardump (data)
    end

    function tdbot_update_callback (data)
      if (data._ == "updateNewMessage") then
        local msg = data.message

        if msg.content._ == "messageText" then
          if msg.content.text == "ping" then
            assert (tdbot_function ({_="sendMessage", chat_id=msg.chat_id, reply_to_message_id=msg.id, disable_notification=false, from_background=true, reply_markup=nil, input_message_content={_="inputMessageText", text="pong", disable_web_page_preview=true, clear_draft=false, entities={}, parse_mode=nil}}, dl_cb, nil))
          end
        end
      end
    end
    ```

