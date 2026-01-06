-- pipe.lua

local rb = nil

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

    rb:SetVelocity(vec2(-3.0, 0.0))
end