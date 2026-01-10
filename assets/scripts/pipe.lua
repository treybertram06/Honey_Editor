-- pipe.lua

Properties = {
    lifetime = 10.0,
}

local rb = nil
local timer = 0.0

function OnCreate()
    rb = self:GetComponent("Rigidbody2D")

    if not rb then
        Honey.Log("Pipe ERROR: Rigidbody2D missing")
        return
    end

    Honey.Log("Pipe initialized")
end

function OnUpdate()
    if Honey.Scene.GetOr("gameOver", false) then
        rb:SetVelocity(vec2(0.0, 0.0))
        rb:SetAngularVelocity(0.0)
        return
    end

    timer = timer + dt

    Honey.Log("Pipe: type(Properties)=" .. tostring(type(Properties)))

    if type(Properties) ~= "table" then
        Honey.Log("Pipe: Properties is not a table, value = " .. tostring(Properties))
    end

    if timer >= Properties.lifetime then
        Honey.Log("Pipe: destroying entity")
        Honey.DestroyEntity(self)
        Honey.Log("Pipe: after destroy")   -- this will never run if destroy causes exit
        return
    end

    rb:SetVelocity(vec2(-3.0, 0.0))
end