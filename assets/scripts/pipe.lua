-- pipe.lua

function OnCreate(self)
    self.rb = self.entity:GetComponent("Rigidbody2D")
    Honey.Log("Pipe initialized")
end

function OnUpdate(self, dt)
    self.rb:SetVelocity(vec2(-3.0, 0.0))
end

