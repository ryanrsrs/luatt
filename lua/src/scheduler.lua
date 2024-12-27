local time = Luatt.time

local scheduler = {}

scheduler.recent_ms = time.millis()

scheduler.pq = Luatt.pkgs.PriorityQueue{
    -- handle timestamp wrap-around
    higherpriority = function(a, b)
        return (a - scheduler.recent_ms) < (b - scheduler.recent_ms)
	end
}

scheduler.interrupts = {}
scheduler.tokens = {}
scheduler.args = {}

-- Called by the main Arduino loop.
-- The C code looks up "scheduler.loop" in the Lua global scope and calls it.
-- Returns the number of millseconds the system should sleep.
function scheduler.loop (ints)
    scheduler.recent_ms = time.millis()

    -- If we have pending interrupts, wake any threads waiting on them.
    if ints ~= 0 then
        for co, co_ints in pairs(scheduler.interrupts) do
            if ints & co_ints ~= 0 then
                -- mark for immediate execution
                scheduler.pq:update(co, scheduler.recent_ms)
            end
        end
    end
    -- TODO: save interrupt bits that nobody is waiting on so they
    -- don't get lost. Needed once my interrupt handlers
    -- get more complex.

    -- Now run any threads that are ready.
    while not scheduler.pq:empty() do
        -- Find next thread to run according to wakeup time.
        local co, t = scheduler.pq:peek()
        local ms = time.millis()
        if t - ms > 0 then
            -- no threads ready to run
            return t - ms
        end

        local r, t_inc, co_ints, args
        co = scheduler.pq:dequeue()
        co_ints = scheduler.interrupts[co] or 0
        scheduler.interrupts[co] = nil

        args = scheduler.args[co]
        scheduler.args[co] = nil

        if coroutine.status(co) == "dead" then
            -- coroutine was closed from another thread
            scheduler.tokens[co] = nil
            coroutine.close(co)
            goto continue
        end

        -- Run thread coroutine.
        Luatt.set_mux_token(scheduler.tokens[co])
        -- scheduler.args[] is only for the first resume (the function args)
        if args == nil then
            -- subsequently, these are returned by yield()
            args = { ms, ints & co_ints }
        end
        r, t_inc, co_ints = coroutine.resume(co, table.unpack(args, 1, args.n))
        Luatt.set_mux_token("sched")

        if coroutine.status(co) == "dead" then
            -- coroutine has exited
            if not r then
                -- because of error
                print("Error: " .. t_inc)
            end
            scheduler.tokens[co] = nil
            coroutine.close(co)
        else
            -- coroutine wants to sleep
            t_inc = t_inc or 0
            scheduler.pq:enqueue(co, time.millis() + math.floor(t_inc))
            if co_ints and co_ints > 0 then
                -- coroutine is listening for interrupts
                scheduler.interrupts[co] = co_ints
            end
        end
        ::continue::
    end
    -- All threads have exited, so just sleep for a second.
    -- We'll be called early if a new thread is created.
    return 1000
end

-- Start a new thread after t_inc milliseconds.
function scheduler.start (co, t_inc, ...)
    scheduler.tokens[co] = Luatt.get_mux_token()
    scheduler.args[co] = table.pack(...)
    scheduler.pq:enqueue(co, time.millis() + math.floor(t_inc or 0))
end

-- Reschedule a thread to run after t_inc milliseconds.
-- Can be used to wake up early.
function scheduler.wake (co, t_inc)
    scheduler.pq:update(co, time.millis() + math.floor(t_inc or 0))
end

Luatt.set_cb_sched_loop(scheduler.loop)

return scheduler
