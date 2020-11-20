local M = {
    omit_excess_refrence = true
}

-- see snapshot.c:L16
local REF_TYPE_TABLE_VALUE = 1
local REF_TYPE_TABLE_KEY = 2
local REF_TYPE_LOCAL = 3
local REF_TYPE_UPVALUE = 4
local REF_TYPE_META = 5

function M.objtypename(objtype)
    if type(objtype) == "string" then
        return objtype
    end
    if objtype == 5 then
        return "table"
    elseif objtype == 6 then
        return "function"
    elseif objtype == 7 then
        return "userdata"
    elseif objtype == 8 then
        return "thread"
    else
        error("invalid objtype: " .. objtype)
    end
end

function M.reftypename(reftype)
    if reftype == REF_TYPE_TABLE_VALUE then
        return "tablevalue"
    elseif reftype == REF_TYPE_TABLE_KEY then
        return "tablekey"
    elseif reftype == REF_TYPE_LOCAL then
        return "local"
    elseif reftype == REF_TYPE_UPVALUE then
        return "upvalue"
    elseif reftype == REF_TYPE_META then
        return "meta"
    else
        error("invalid reftype: " .. reftype)
    end
end

M.translate = {
    ["[registry].mainthread"] = "_M",  -- lua5.1
    ["[registry].[1]"] = "_M",
    ["[registry].[2]"] = "_G",
    ["[registry]._LOADED"] = "package.loaded",
    ["[registry]._PRELOAD"] = "package.preload",
}

function M.refname(ref)
    local name
    if ref.type == REF_TYPE_TABLE_VALUE then
        name = ref.name
    elseif ref.type == REF_TYPE_TABLE_KEY then
        name = ref.name
    elseif ref.type == REF_TYPE_LOCAL then
        name = string.format("<%s %s>",M.reftypename(ref.type),ref.name)
    elseif ref.type == REF_TYPE_UPVALUE then
        name = string.format("<%s %s>",M.reftypename(ref.type),ref.name)
    elseif ref.type == REF_TYPE_META then
        name = ref.name
    else
        assert(false)
    end
    return name
end

function M.objname(object,S)
    if object.name then
        return object.name
    end
    local name
    local ref = object.reflist[object.refindex]
    if object.depth == 0 then
        name = ref.name
    else
        local refobj = S[ref.obj]
        name = string.format("%s.%s",M.objname(refobj,S),M.refname(ref))
    end
    local trans = M.translate[name]
    if trans then
        name = trans
    end
    assert(name ~= nil)
    object.name = name
    return name
end

function M.pretty_object(object,S)
    object.name = M.objname(object,S)
    object.type = M.objtypename(object.type)
    object.refindex = nil
    object.depth = nil
    if object.source == "" then
        object.source = nil
    end
end

function M.pretty_object_reflist(object,S)
    for i,ref in ipairs(object.reflist) do
        if ref.obj == "(nil)" then      -- root
            object.reflist[i] = M.refname(ref)
        else
            local refobj = S[ref.obj]
            object.reflist[i] = string.format("%s.%s",refobj.name,M.refname(ref))
        end
        if M.omit_excess_refrence and i >= 10 then
            if i == 10 then
                object.reflist[i] = "..."
            else
                object.reflist[i] = nil
            end
        end
    end
end

function M.diff(S1,S2,pretty)
    local diff = {}
    for id,object in pairs(S2) do
        if S1[id] == nil then
            diff[id] = object
        end
    end
    if not pretty then
        return diff
    end
    for id,object in pairs(S2) do
        M.pretty_object(object, S2)
    end
    for id,object in pairs(diff) do
        M.pretty_object_reflist(object,S2)
    end
    for id,object in pairs(diff) do
        object.name = nil
    end
    return diff
end

function M.dump(t,space,name)
    if type(t) ~= "table" then
        return tostring(t)
    end
    space = space or ""
    name = name or ""
    local cache = { [t] = "."}
    local function _dump(t,space,name)
        local temp = {}
        for k,v in pairs(t) do
            local key = tostring(k)
            if cache[v] then
                table.insert(temp,"+" .. key .. " {" .. cache[v].."}")
            elseif type(v) == "table" then
                local new_key = name .. "." .. key
                cache[v] = new_key
                table.insert(temp,"+" .. key .. _dump(v,space .. (next(t,k) and "|" or " " ).. string.rep(" ",#key),new_key))
            else
                table.insert(temp,"+" .. key .. " [" .. tostring(v).."]")
            end
        end
        return table.concat(temp,"\n"..space)
    end
    return _dump(t,space,name)
end

function M.sortN(n,tbl,cmp)
    local ret = {}
    local len = 0
    local function bubble(pos,v)
        while pos > 1 do
            if cmp(v,ret[pos-1]) then
                ret[pos] = ret[pos-1]
                pos = pos - 1
            else
                break
            end
        end
        ret[pos] = v
    end
    for k,v in pairs(tbl) do
        if len < n then
            len = len + 1
            bubble(len,v)
        elseif cmp(v,ret[len]) then
            bubble(len,v)
        end
    end
    return ret
end

function M.refcount_topN(n,S,pretty)
    local list = {}
    for id,object in pairs(S) do
        list[#list+1] = object
    end
    list = M.sortN(n,list,function (a,b)
        local refcount1 = #a.reflist
        local refcount2 = #b.reflist
        if refcount1 == refcount2 then
            return false
        end
        return refcount1 > refcount2
    end)
    if not pretty then
        return list
    end
    for id,object in pairs(S) do
        M.pretty_object(object, S)
   end
    for i,object in ipairs(list) do
        M.pretty_object_reflist(object,S)
    end
    for i,object in ipairs(list) do
        object.name = nil
    end
    return list
end

function M.tablecount_topN(n,S,pretty)
    local list = {}
    local g = tostring(_G):sub(8,-1)
    for id,object in pairs(S) do
        if id ~= g then
            list[#list+1] = object
        end
    end
    list = M.sortN(n,list,function (a,b)
        local tablecount1 = a.tablecount
        local tablecount2 = b.tablecount
        if tablecount1 == tablecount2 then
            return false
        end
        return tablecount1 > tablecount2
    end)
    if not pretty then
        return list
    end
    for id,object in pairs(S) do
        M.pretty_object(object, S)
    end
    for i,object in ipairs(list) do
        M.pretty_object_reflist(object,S)
    end
    for i,object in ipairs(list) do
        object.name = nil
    end
    return list
end

return M