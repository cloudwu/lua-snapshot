local snapshot = require "snapshot"
local print_r = require "print_r"

local S1 = snapshot()

local tmp = {
    player = {
        uid = 1,
        camps = {
            {campid = 1},
            {campid = 2},
        },
    },
    player2 = {
        roleid = 2,
    },
    [3] = {
        player1 = 1,
    },
}

local a = {}
local c = {}
a.b = c
c.d = a

local msg = "bar"
local foo = function()
    print(msg)
end

local co = coroutine.create(function ()
    print("hello world")
end)

local S2 = snapshot()

local diff = {}
for k,v in pairs(S2) do
	if not S1[k] then
        diff[k] = v
	end
end

print_r(diff)
print("===========================")

local function trim(s)
    return (s:gsub("^%s*(.-)%s*$", "%1"))
end

local function cleanup_key_value(input)
    local ret = {}
    for k, v in pairs(input) do
        local key = tostring(k)
        local clean_key = key:gmatch("userdata: 0x(%w+)")()
        local val_type
        if v:find("^table") then
            val_type = "table"
        elseif v:find("^func:") then
            val_type = "func"
        elseif v:find("^thread:") then
            val_type = "thread"
        else
            val_type = "userdata"
        end
        local parent = v:match("0x(%w+) :")
        local _, finish = v:find("0x(%w+) : ")
        local extra = v:sub(finish + 1, #v)
        local val_key = extra:match("(%w+) :")
        local trim_extra = trim(extra)
        if not val_key then
            val_key = trim_extra
        end
        ret[clean_key] = {
            val_type = val_type,
            parent = parent,
            extra = trim_extra,
            key = val_key,
        }
    end
    return ret
end

local function reduce(input_diff)
    local ret = {}
    local count = 0
    for self_addr, info in pairs(input_diff) do
        local parent = info.parent
        if not input_diff[parent] then
            ret[self_addr] = info
            count = count + 1
        else
            if not ret[parent] then
                ret[parent] = input_diff[parent]
                count = count + 1
            end
            local key = info.key
            if not ret[parent].values then
                ret[parent].values = {}
            end
            ret[parent].values[key] = info
        end
    end
    return ret, count
end

local unwanted_key = {
    --extra = 1,
    --key = 1,
    parent = 1,
}
local function cleanup_forest(forest)
    for k, v in pairs(forest) do
        if unwanted_key[k] then
            forest[k] = nil
        else
            if type(v) == "table" then
                cleanup_forest(v)
            end
        end
    end
end

local function construct_indentation(input_diff)
    local clean_diff = cleanup_key_value(input_diff)
    --print_r(clean_diff)
    --print("====================")
    local forest, count = reduce(clean_diff)
    --print_r(forest)
    --print("--------------------")
    local new_forest, new_count = reduce(forest)
    while new_count ~= count do
        forest, count = new_forest, new_count
        new_forest, new_count = reduce(new_forest)
    end
    cleanup_forest(new_forest)
    return new_forest
end

local result = construct_indentation(diff)
print_r(result)
