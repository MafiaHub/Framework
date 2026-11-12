-- A Lua resource. Every global here has the name it has in JavaScript.
local spawn = Vector3.new(10, 0, 5)
local greeted = 0

-- Other resources reach this through Imports.get('lua-example'), whatever
-- language they are written in.
Exports.register('distanceToSpawn', function(position)
    return spawn:distance(position)
end)

Events.on('example:hello', function(who)
    greeted = greeted + 1
    -- A table arrives in JavaScript as an object, a Vector3 as a Vector3.
    Events.emit('example:welcome', { name = who.name, count = greeted, spawn = spawn })
end)

-- The timers of JavaScript, with the delay in milliseconds.
local beats = 0
local heartbeat
heartbeat = setInterval(function()
    beats = beats + 1
    if beats == 3 then
        clearInterval(heartbeat)
        print('ready after ' .. beats .. ' beats')
        Events.emit('example:ready')
    end
end, 10)

Events.on('resourceStop', function(name)
    if name == 'lua-example' then
        print('greeted ' .. greeted .. ' resource(s)')
    end
end)
