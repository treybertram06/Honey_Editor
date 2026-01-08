-- traceCircle.lua

local angle = 0.0
local radius = 1.0
local speed = 3.0  -- radians per second
local x_offset = 0.0
local y_offset = 0.0

function OnCreate()
    Honey.Log("circle tracer initialized")
end

function OnUpdate()
    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Floor ERROR: Transform missing")
        return
    end

    if x_offset == 0.0 and y_offset == 0.0 then
        x_offset = transform.translation.x
        y_offset = transform.translation.y
    end

    angle = angle + speed * dt

    transform.translation.x = math.cos(angle) * radius
    transform.translation.y = math.sin(angle) * radius
end