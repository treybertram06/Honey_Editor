-- pipe.lua

Properties = {
    lifetime = 10.0,
}

local rb = nil
local timer = 0.0
local velocity = vec2(-3.0, 0.0)

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
        rb:SetVelocity(velocity)
        velocity.x = velocity.x * 0.9999 * dt
        rb:SetAngularVelocity(0.0)
        return
    end

    timer = timer + dt

    if timer >= Properties.lifetime then
        Honey.DestroyEntity(self)
        return
    end

    rb:SetVelocity(velocity)
end