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
    if not rb then return end

    rb:SetVelocity(vec2(-3.0, 0.0))
end