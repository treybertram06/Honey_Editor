-- bird.lua

Properties = {
    flapStrength = 5.0,
}

local rb = nil
local transform = nil

function OnCreate()
    rb = self:GetComponent("Rigidbody2D")
    transform = self:GetTransform()

    if not rb then
        Honey.Log("Bird ERROR: Rigidbody2D missing")
    end

    Honey.Log("Bird controller initialized")
end

function OnUpdate()
    if not rb then return end

    -- Flap
    if Honey.IsKeyPressed(Key.Space) then
        local vel = rb:GetVelocity()
        vel.y = Properties.flapStrength
        rb:SetVelocity(vel)
    end

    -- Rotate based on velocity
    local vel = rb:GetVelocity()
    transform.rotation.z = math.atan(vel.y * 0.2)
end

function OnCollisionBegin(other)
    --if (other:GetTag() == "KinPipe") then
        Honey.Scene.Set("gameOver", true)
    --end

end