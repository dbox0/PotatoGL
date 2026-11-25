#include "Register.h"

#include "game/scripts/Rotator.h"

#include "engine/script/ScriptRunner.h"

void registerScripts(ScriptRunner& scripts)
{
    scripts.add<Rotator>("Rotator")
        .field<&Rotator::axis>("Axis", 0.01f)
        .field<&Rotator::degreesPerSec>("Degrees/sec", 1.0f);
}
