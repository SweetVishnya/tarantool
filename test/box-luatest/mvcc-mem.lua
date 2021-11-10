#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen                 = os.getenv("LISTEN"),
    memtx_use_mvcc_engine  = true
}

require('console').listen(os.getenv('ADMIN'))