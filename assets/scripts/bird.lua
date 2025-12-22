-- bird.lua

local rb = nil

function OnCreate(self)
    rb = self:GetComponent("Rigidbody2D")
    Honey.Log("Bird controller initialized")
end

function OnUpdate(self, dt)
    -- Flap when pressing space
    if Honey.IsKeyPressed(Key.Space) then
        local vel = rb:GetVelocity()
        vel.y = 5.0 -- upward impulse
        rb:SetVelocity(vel)
    end

    -- Optional: rotate the bird depending on velocity
    local transform = self:GetTransform()
    local vel = rb:GetVelocity()
    transform.rotation.z = math.atan(vel.y * 0.2)
end

function OnCollisionBegin(self, other)
    Honey.Log("Bird collided with " .. tostring(other))
    -- Here you could trigger game over logic
end
