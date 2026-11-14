// A JavaScript resource talking to the Lua resource next to it.
const example = Imports.get('lua-example');

Events.on('example:welcome', (welcome) => {
    const spawn = welcome.spawn instanceof Vector3 ? `(${welcome.spawn.x}, ${welcome.spawn.y}, ${welcome.spawn.z})` : 'unknown';
    console.log(`welcome ${welcome.name}, number ${welcome.count}, spawn at ${spawn}`);
});

Events.on('example:ready', async () => {
    // An export written in Lua is called like any other. Awaiting it covers
    // an export that waits before it answers.
    const distance = await example.distanceToSpawn(new Vector3(13, 4, 5));
    console.log(`distance to spawn: ${distance}`);
    Events.emit('example:hello', { name: 'js-example' });
});
