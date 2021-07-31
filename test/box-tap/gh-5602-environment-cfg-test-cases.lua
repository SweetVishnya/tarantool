local tap = require('tap')

local test = tap.test('gh-5602')

local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})

-- Check that environment cfg values are set correctly.
if arg[1] == '1' then
    test:plan(6)
    test:ok(status, 'box.cfg is successful')
    test:is(box.cfg['listen'], '3301', 'listen')
    test:is(box.cfg['readahead'], 10000, 'readahead')
    test:is(box.cfg['strip_core'], false, 'strip_core')
    test:is(box.cfg['log_format'], 'json', 'log_format is not set')
    test:is(box.cfg['log_nonblock'], false, 'log_nonblock')
end
if arg[1] == '2' then
    test:plan(7)
    test:ok(status, 'box.cfg is successful')
    test:is(box.cfg['listen'], '3301', 'listen')
    local replication = box.cfg['replication']
    test:is(type(replication), 'table', 'replication is table')
    test:ok(replication[1] == '0.0.0.0:12345' or
            replication[1] == '1.1.1.1:12345', 'replication URI 1')
    test:ok(replication[2] == '0.0.0.0:12345' or
            replication[2] == '1.1.1.1:12345', 'replication URI 2')
    test:is(box.cfg['replication_connect_timeout'], 0.01,
            'replication_connect_timeout')
    test:is(box.cfg['replication_synchro_quorum'], '4 + 1',
            'replication_synchro_quorum')
end

-- Check that box.cfg{} values are more prioritized than
-- environment cfg values.
if arg[1] == '3' then
    test:plan(3)
    test:ok(status, 'box.cfg is successful')
    test:is(box.cfg['background'], false,
            'box.cfg{} background value is prioritized')
    test:is(box.cfg['vinyl_timeout'], 70.1,
            'box.cfg{} vinyl_timeout value is prioritized')
end

local err_msg_fmt = 'Environment variable TT_%s has incorrect value for ' ..
    'option "%s": should be %s'

-- Check bad environment cfg values.
if arg[1] == '4' then
    test:plan(2)
    test:ok(not status, 'box.cfg is not successful')
    local exp_err = err_msg_fmt:format('SQL_CACHE_SIZE', 'sql_cache_size',
        'convertible to a number')
    local err_msg = tostring(err)
    while err_msg:find('^.-:.-: ') do
        err_msg = err_msg:gsub('^.-:.-: ', '')
    end
    test:is(err_msg, exp_err, 'bad sql_cache_size value')
end
if arg[1] == '5' then
    test:plan(2)
    test:ok(not status, 'box.cfg is not successful')
    local exp_err = err_msg_fmt:format('STRIP_CORE', 'strip_core',
        '"true" or "false"')
    local err_msg = tostring(err)
    while err_msg:find('^.-:.-: ') do
        err_msg = err_msg:gsub('^.-:.-: ', '')
    end
    test:is(err_msg, exp_err, 'bad strip_core value')
end
if arg[1] == '6' then
    test:plan(5)
    test:ok(status, 'box.cfg is successful')
    test:is(box.cfg['listen'], '3301', 'listen')
    test:is(box.cfg['background'], true, 'background')
    test:is(box.cfg['pid_file'], 'tarantool.pid', 'pid file')
    test:is(box.cfg['log'], 'tarantool.log', 'log file')
end

os.exit(test:check() and 0 or 1)
