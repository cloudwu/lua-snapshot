local snapshot = require "snapshot"
local snapshot_utils = require "snapshot_utils"
local construct_indentation = snapshot_utils.construct_indentation
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
local player1 = {tag = "player",id = 1}
local player2 = {tag = "player",id = 2}
local player11 = {tag = "player",id = 11}
local test = function ()
    print("test")
end
snapshot(function (obj)
    if obj == _G or obj == debug.getregistry() then
        return false
    end
    if type(obj) == "table" and obj.tag == "player" then
        if obj.id > 10 then
            return true
        else
            return false
        end
    end
    return false
end)
local S3 = snapshot()
local diff23 = {}
for k,v in pairs(S3) do
    if not S2[k] then
        diff23[k] = v
    end
end

print_r(diff23)
snapshot(false)


print("===========================")

local result = construct_indentation(diff)
print_r(result)
