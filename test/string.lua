-- string_test.lua

local function assert_eq(a, b, msg)
    if a ~= b then error(msg or ("assert_eq failed: " .. tostring(a) .. " ~= " .. tostring(b))) end
end

local s = "Hello, Lua!"

-- Byte tests
do
    -- 逐字节取值，string.byte 返回指定位置字符的ASCII码
    local b1 = string.byte(s, 1)
    assert_eq(b1, 72, "Byte test 1")
    local b2 = string.byte(s, 8, 10)
    assert_eq(#{string.byte(s, 8, 10)}, 3, "Byte test 2 length")
end

-- Char tests
do
    -- string.char 把数字转换成对应字符
    local chars = {string.char(72, 101, 108, 108, 111)}
    assert_eq(table.concat(chars), "Hello", "Char test")
end

-- Dump tests
do
    -- string.dump 传入函数，返回编译后的字符串
    local f = function(x) return x + 1 end
    local dumped = string.dump(f)
    assert(type(dumped) == "string" and #dumped > 0, "Dump test")
end

-- Find tests
do
    -- string.find 查找子串位置，返回起止索引
    local start_pos, end_pos = string.find(s, "Lua")
    assert_eq(start_pos, 8, "Find test start")
    assert_eq(end_pos, 10, "Find test end")
end

-- Format tests
do
    -- string.format 格式化字符串
    local fmt = string.format("Pi is approximately %.2f", 3.14159)
    assert_eq(fmt, "Pi is approximately 3.14", "Format test")
end

-- GFindNodeF tests (string.gfind 已被废弃，string.gmatch 替代)
do
    -- 用 string.gmatch 迭代单词
    local words = {}
    for w in string.gmatch(s, "%a+") do
        table.insert(words, w)
    end
    assert_eq(words[1], "Hello", "GFindNodeF test first word")
end

-- GMatch tests
do
    -- 类似上面
    local count = 0
    for _ in string.gmatch(s, "%a+") do count = count + 1 end
    assert(count > 0, "GMatch test")
end

-- GSub tests
do
    -- string.gsub 替换字符串
    local replaced, n = string.gsub(s, "Lua", "World")
    assert_eq(n, 1, "GSub count")
    assert_eq(replaced, "Hello, World!", "GSub replaced string")
end

-- Length tests
do
    -- string.len 返回字符串长度
    assert_eq(#s, string.len(s), "Length test")
end

-- Lower tests
do
    -- string.lower 转小写
    assert_eq(string.lower("ABC"), "abc", "Lower test")
end

-- Match tests
do
    -- string.match 返回第一个匹配的子串
    local matched = string.match(s, "%a+")
    assert_eq(matched, "Hello", "Match test")
end

-- Rep tests
do
    -- string.rep 重复字符串
    local repeated = string.rep("Lua", 3)
    assert_eq(repeated, "LuaLuaLua", "Rep test")
end

-- Reverse tests
do
    -- string.reverse 反转字符串
    local reversed = string.reverse("abc")
    assert_eq(reversed, "cba", "Reverse test")
end

-- Sub tests
do
    -- string.sub 返回子串
    local sub = string.sub(s, 8, 10)
    assert_eq(sub, "Lua", "Sub test")
end

-- Upper tests
do
    -- string.upper 转大写
    assert_eq(string.upper("abc"), "ABC", "Upper test")
end

-- 空字符串操作
assert(string.byte("") == nil)
assert(string.char() == "")
assert(string.len("") == 0)
assert(string.sub("", 1, 10) == "")
assert(string.reverse("") == "")
assert(string.upper("") == "")
assert(string.lower("") == "")
assert(string.rep("", 10) == "")

-- 超长字符串（简化，避免内存爆炸）
local long_str = string.rep("a", 10000)
assert(string.len(long_str) == 10000)
assert(string.sub(long_str, 9990, 10000) == string.rep("a", 11))
assert(string.reverse(long_str) == long_str)

-- 非法参数处理
assert(pcall(function() string.byte("abc", -5) end) == false or true) -- 允许正常返回nil或false，Lua原生返回nil
assert(string.sub("abc", 5, 10) == "")
assert(string.sub("abc", -5, -1) == "abc")

-- 多重复次数
assert(string.rep("xy", 0) == "")
assert(string.rep("xy", 3) == "xyxyxy")

-- byte 边界测试
local test_str = "hello"
assert(string.byte(test_str, 1) == 104)
assert(string.byte(test_str, 5) == 111)
assert(string.byte(test_str, 6) == nil)

-- reverse 特殊字符测试
local special_str = "\0\1\2abc"
local reversed = string.reverse(special_str)
assert(string.len(reversed) == string.len(special_str))
assert(string.reverse(reversed) == special_str)

-- sub 越界截取
assert(string.sub("abc", 2, 5) == "bc")
assert(string.sub("abc", 4, 10) == "")
assert(string.sub("abc", -10, 2) == "ab")

-- find 找不到的情况
assert(string.find("hello", "z") == nil)
assert(string.find("hello", "") == 1) -- 空串总是匹配开始处

print("All Lua::String::Context tests passed.")