test_run = require('test_run').new()
net_box = require('net.box')
fio = require('fio')

default_listen_addr = box.cfg.listen
test_run:cmd("setopt delimiter ';'")
-- Create table which contain several listening uri,
-- according to template @a addr. It's simple adds
-- capital letters in alphabetical order to the
-- template.
function create_uri_table(addr, count)
    local uris_table = {}
    local path_table = {}
    local ascii_A = 97
    for i = 1, count do
        local ascii_code = ascii_A + i - 1
        local letter = string.upper(string.char(ascii_code))
        path_table[i] = addr .. letter
        uris_table[i] = "unix/:" .. addr .. letter
    end
    return uris_table, path_table
end;
function check_connection(port)
    local conn = net_box.connect(port)
    assert(conn:ping())
    conn:close()
end;
test_run:cmd("setopt delimiter ''");

-- Check connection if listening uri passed as a single port number.
port_number = 0
box.cfg{listen = port_number}
check_connection(box.info.listen)

-- Check connection if listening uri passed as a single string.
unix_socket_path = "./tarantool" .. "A"
box.cfg{listen = "unix/:" .. unix_socket_path}
assert(box.cfg.listen == "unix/:" .. unix_socket_path)
assert(box.info.listen:match("unix/:"))
check_connection("unix/:" .. unix_socket_path)

-- Check connection if listening uri passed as a table of port numbers.
port_numbers = {0, 0, 0, 0, 0}
box.cfg{listen = port_numbers}
for i, _ in ipairs(port_numbers) do \
    check_connection(box.info.listen[i]) \
end

-- Check connection if listening uri passed as a table of strings.
uri_table, path_table = create_uri_table("./tarantool", 5)
box.cfg{listen = uri_table}
for i, uri in ipairs(uri_table) do \
    assert(box.cfg.listen[i] == uri) \
    assert(box.info.listen[i]:match("unix/:")) \
    assert(fio.path.exists(path_table[i])) \
    check_connection(uri) \
end

box.cfg{listen = default_listen_addr}
for i, path in ipairs(path_table) do \
    assert(not fio.path.exists(path)) \
end
assert(fio.path.exists(default_listen_addr))

-- Special test case to check that all unix socket paths deleted
-- in case when `listen` fails because of invalid uri. Iproto performs
-- `bind` and `listen` operations sequentially to all uris from the list,
-- so we need to make sure that all resources for those uris for which
-- everything has already completed will be successfully cleared in case
-- of error for one of the next uri in list.
uri_table, path_table = create_uri_table("./tarantool", 5)
uri_table[#uri_table + 1] = "baduri:1"
uri_table[#uri_table + 1] = "unix/:" .. "./tarantool" .. "X"

-- can't resolve uri for bind
box.cfg{listen = uri_table}
for i, path in ipairs(path_table) do \
    assert(not fio.path.exists(path)) \
end
assert(box.cfg.listen == default_listen_addr)

test_run:cmd("push filter 'error: bind, called on fd [0-9]+' \
              to 'error: bind, called on fd <number>'")

-- Special test case when we try to listen several identical URIs
uri = "unix/:" .. "./tarantool"
box.cfg{listen = {uri, uri, uri}}
assert(not fio.path.exists(uri))
assert(box.cfg.listen == default_listen_addr)
