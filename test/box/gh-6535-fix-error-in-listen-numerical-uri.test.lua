test_run = require('test_run').new()
net_box = require('net.box')
fio = require('fio')

old_listen = box.cfg.listen
unix_socket_path = "./tarantool" .. "A"
box.cfg{listen = "unix/:" .. unix_socket_path}
box.cfg{listen = 0}
conn = net_box.connect(box.info.listen)
assert(conn:ping())
conn:close()
box.cfg{listen = old_listen}
