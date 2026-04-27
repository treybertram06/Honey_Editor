-- lightWander.lua

Properties = {
    moveRange = 5.0,
    moveSpeed = 5.0,
    targetThreshold = 0.15,
}

local origin_x = 0.0
local origin_y = 0.0
local origin_z = 0.0

local target_x = 0.0
local target_y = 0.0
local target_z = 0.0

local function pick_new_target()
    target_x = origin_x + Honey.Random(-Properties.moveRange, Properties.moveRange)
    target_y = origin_y + Honey.Random(-Properties.moveRange, Properties.moveRange)
    target_z = origin_z + Honey.Random(-Properties.moveRange, Properties.moveRange)
end

function OnCreate()
    local transform = self:GetTransform()
    if not transform then
        Honey.Log("lightWander ERROR: Transform missing")
        return
    end

    origin_x = transform.translation.x
    origin_y = transform.translation.y
    origin_z = transform.translation.z

    pick_new_target()
    Honey.Log("lightWander initialized")
end

function OnUpdate()
    local transform = self:GetTransform()
    if not transform then
        Honey.Log("lightWander ERROR: Transform missing")
        return
    end

    local dx = target_x - transform.translation.x
    local dy = target_y - transform.translation.y
    local dz = target_z - transform.translation.z
    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)

    if distance <= Properties.targetThreshold then
        pick_new_target()
        return
    end

    local step = Properties.moveSpeed * dt
    if step >= distance then
        transform.translation.x = target_x
        transform.translation.y = target_y
        transform.translation.z = target_z
        pick_new_target()
        return
    end

    local inv_distance = 1.0 / distance
    transform.translation.x = transform.translation.x + dx * inv_distance * step
    transform.translation.y = transform.translation.y + dy * inv_distance * step
    transform.translation.z = transform.translation.z + dz * inv_distance * step
end
