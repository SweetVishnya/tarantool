#!/usr/bin/env tarantool


local fiber = require('fiber')
local process_timeout = require('process_timeout')
local tap = require('tap')
local fio = require('fio')
local ffi = require('ffi')

local TARANTOOL_PATH = arg[-1]
local output_file = fio.abspath('gh-2717-no-quit-sigint.txt')
local cmd_end = (' >%s & echo $!'):format(output_file)

-- Like a default timeout for `cond_wait` in test-run
local process_waiting_timeout = 20.0
local file_read_timeout = 20.0
local file_read_interval = 0.2
local file_open_timeout = 20.0

-- Each testcase consists of:
--  * cmd_args - command line arguments for tarantool binary
--  * stdin - stdin for tarantool
--  * interactive - true if interactive mode expected
--  * empty_output - true if command should have empty output
local testcases = {
    {
        cmd_args = ' -i -e "local fiber = require(\'fiber\') fiber.sleep(10)"',
        stdin = 'tty',
        interactive = true,
        empty_output = true
    },
}

local test = tap.test('gh-2717')

test:plan(#testcases)
for _, cmd in pairs(testcases) do
    local full_cmd = ''
    if cmd.stdin == 'tty' then
        cmd.stdin = ''
        full_cmd = 'ERRINJ_STDIN_ISATTY=1 '
    else
        cmd.stdin = '< ' .. cmd.stdin
    end

    local full_cmd = full_cmd .. ('%s %s %s %s'):format(
            TARANTOOL_PATH,
            cmd.cmd_args,
            cmd.stdin,
            cmd_end
    )


    test:test(full_cmd, function(test)
        test:plan(cmd.interactive and 1 or 2)
        local pid = tonumber(io.popen(full_cmd):read("*line"))
        assert(pid, "pipe error for: " .. cmd.cmd_args)


        fiber.sleep(1)
        ffi.C.kill(pid, 2)

        local fh = process_timeout.open_with_timeout(output_file,
                file_open_timeout)
        assert(fh, 'error while opening ' .. output_file)

        if cmd.interactive then
            local data = process_timeout.read_with_timeout(fh,
                    file_read_timeout,
                    file_read_interval)
            test:like(data, 'tarantool>', 'interactive mode detected - process keeps living')
        end

        if process_timeout.process_is_alive(pid) then
            ffi.C.kill(pid, 9)
        end
        fh:close()
        os.remove(output_file)
    end)
end


os.exit(res and 0 or 1)