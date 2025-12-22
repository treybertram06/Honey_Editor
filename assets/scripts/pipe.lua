-- pipe.lua

local rb = nil

function OnCreate(self)
    rb = self:GetComponent("Rigidbody2D")
end

function OnUpdate(self, dt)
    rb:SetVelocity(vec2(-3.0, 0.0))
end

