env = require('test_run')
test_run = env.new()
test_run:cmd("create server mvcc_mem with script='box/mvcc-mem.lua'")
test_run:cmd("start server mvcc_mem")
test_run:cmd("switch mvcc_mem")

txn_proxy = require('txn_proxy')

tx2 = txn_proxy.new()
tx3 = txn_proxy.new()

box.stat.tx()

s = box.schema.space.create('test')
_ = s:create_index('pk')

tx1:begin()
tx1('s:replace{1, 1}')
box.stat.tx()
tx2:begin()
tx2('s:replace{2, 2}')
tx2('s:replace{1, 2}')
-- Pin a tuple
box.stat.tx()
tx1:commit()
tx2:commit()
box.stat.tx()
-- Make GC delete pinned tuple
s:replace{3, 3}
s:replace{4, 4}
box.stat.tx()

test_run:cmd("switch default")
test_run:cmd("stop server mvcc_mem")
test_run:cmd("cleanup server mvcc_mem")
