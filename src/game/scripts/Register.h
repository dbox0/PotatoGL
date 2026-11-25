#pragma once

class ScriptRunner;

// Explicit list on purpose: self-registering statics get dropped by the linker in static libs
void registerScripts(ScriptRunner& scripts);
