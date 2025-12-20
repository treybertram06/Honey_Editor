function OnCreate(entity)
    Honey.Log("Lua Script Loaded!")
end

function OnUpdate(entity, dt)
    local t = entity:GetTransform()
    t.translation.x = t.translation.x + 1.0 * dt
end
