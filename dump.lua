local snapshot = require "snapshot"
local snapshot_utils = require "snapshot_utils"

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
    [6729207776230248443] = {
        player1 = 1,
    },
}

local a = {}
local c = {}
a.b = c
c.d = a
g_var = {}
local d = g_var

local msg = "bar"
local foo = function()
    print(msg)
end

local co = coroutine.create(function ()
    print("hello world")
end)
local S2 = snapshot()

print(snapshot_utils.dump(snapshot_utils.diff(S1,S2,true)))
S1 = nil

print("=========test ignore function=========")
local player1 = {tag = "player",id = 1}
local player2 = {tag = "player",id = 2}
local player11 = {tag = "player",id = 11}
local function ignore(obj)
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
end
local S3 = snapshot(ignore)
print(snapshot_utils.dump(snapshot_utils.diff(S2,S3,true)))
S3 = nil

print("=========test refcount topN=========")
--snapshot_utils.omit_excess_refrence = false
local topN = 3
local S4 = snapshot()
print(snapshot_utils.dump(snapshot_utils.refcount_topN(topN,S4,true)))
S4 = nil

print("=========test tablecount topN=========")
local S5 = snapshot()
print(snapshot_utils.dump(snapshot_utils.tablecount_topN(topN,S5,true)))
S5 = nil