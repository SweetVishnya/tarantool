local t = require('luatest')
local log = require('log')
local Cluster =  require('test.luatest_helpers.cluster')
local asserts = require('test.luatest_helpers.asserts')
local helpers = require('test.luatest_helpers')

local pg = t.group('txm')

pg.before_each(function(cg)
    cg.cluster = Cluster:new({})
    local box_cfg = {
        memtx_use_mvcc_engine = true,
        wal_mode = 'none',
    }
    cg.txm_server = cg.cluster:build_server({alias = 'txm_server', box_cfg = box_cfg})
    cg.cluster:add_server(cg.txm_server)
    cg.cluster:start()
end)

pg.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

function dump(o)
    if type(o) == 'table' then
        local s = '{ '
        for k,v in pairs(o) do
            if type(k) ~= 'number' then k = '"'..k..'"' end
            s = s .. '['..k..'] = ' .. dump(v) .. ','
        end
            return s .. ' } '
        else
            return tostring(o)
    end
end

pg.test_txm_mem = function(cg)
    local stats = cg.txm_server:eval('return box.stat.tx()')
    print(dump(stats))
    cg.txm_server:eval("txn_proxy = require('txn_proxy')")
    cg.txm_server:eval('tx1 = txn_proxy.new()')
end

