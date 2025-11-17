#pragma once

#include <Honey.h>
#include <glm/glm.hpp>

#include "Honey/scripting/mono_script_engine.h"

class Test : public Honey::ScriptableEntity {
public:
    void on_create() override {

    }

    void on_destroy() override {}

    void on_update(Honey::Timestep ts) override {
        //Honey::Scripting::MonoScriptEngine::execute_method("Honey", "TestScript", "Hello");
    }


};

