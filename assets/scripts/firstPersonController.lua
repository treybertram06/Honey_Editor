-- firstPersonController.lua

Properties = {
    pitchLimit = 89.000000,
    moveSpeed = 5.000000,
    mouseSensitivity = 0.150000,
    sprintSpeed = 25.000000,
}

local transform  = nil
local yaw        = 0.0
local pitch      = 0.0
local lastMouseX = 0.0
local lastMouseY = 0.0
local firstFrame = true

-- Raw keycodes not yet exposed in the Key table
local KEY_LSHIFT  = 340
local KEY_LCTRL   = 341
local KEY_ESCAPE  = 256

function OnCreate()
    transform = self:GetTransform()

    lastMouseX, lastMouseY = Honey.GetMousePosition()

    -- Seed yaw/pitch from the entity's starting rotation so the camera
    -- doesn't snap on the first frame.
    yaw   = transform.rotation.y
    pitch = transform.rotation.x

    Honey.CaptureMouse()
    Honey.Log("FPS controller initialized")
end

function OnUpdate()
    if not transform then return end

    -- Mouse look ----------------------------------------------------------
    local mx, my = Honey.GetMousePosition()
    local dx = mx - lastMouseX
    local dy = my - lastMouseY
    lastMouseX = mx
    lastMouseY = my

    -- Discard the huge delta that appears on the very first frame
    if firstFrame then
        dx = 0
        dy = 0
        firstFrame = false
    end

    yaw   = yaw   - dx * Properties.mouseSensitivity
    pitch = pitch - dy * Properties.mouseSensitivity
    pitch = math.max(-Properties.pitchLimit, math.min(Properties.pitchLimit, pitch))

    if Honey.IsMouseCaptured() then
        transform.rotation.x = pitch
        transform.rotation.y = yaw
    end

    -- Movement ------------------------------------------------------------
    local speed = Properties.moveSpeed
    if Honey.IsKeyPressed(KEY_LSHIFT) then
        speed = Properties.sprintSpeed
    end

    local yawRad = math.rad(yaw)
    -- Forward/right vectors derived from yaw only so vertical look doesn't
    -- affect horizontal movement (standard FPS feel).
    local fwdX = -math.sin(yawRad)
    local fwdZ = -math.cos(yawRad)
    local rgtX =  math.cos(yawRad)
    local rgtZ = -math.sin(yawRad)

    local mx, my, mz = 0, 0, 0

    if Honey.IsKeyPressed(Key.W) then mx = mx + fwdX; mz = mz + fwdZ end
    if Honey.IsKeyPressed(Key.S) then mx = mx - fwdX; mz = mz - fwdZ end
    if Honey.IsKeyPressed(Key.A) then mx = mx - rgtX; mz = mz - rgtZ end
    if Honey.IsKeyPressed(Key.D) then mx = mx + rgtX; mz = mz + rgtZ end

    -- Vertical (world-space up/down)
    if Honey.IsKeyPressed(Key.Space)  then my = my + 1.0 end
    if Honey.IsKeyPressed(KEY_LCTRL)  then my = my - 1.0 end

    -- Release mouse capture
    if Honey.IsKeyPressed(KEY_ESCAPE) then Honey.ReleaseMouse() end

    -- Normalize diagonal movement
    local len = math.sqrt(mx*mx + my*my + mz*mz)
    if len > 0.001 then
        mx = mx / len
        my = my / len
        mz = mz / len
    end

    local step = speed * dt
    transform.translation.x = transform.translation.x + mx * step
    transform.translation.y = transform.translation.y + my * step
    transform.translation.z = transform.translation.z + mz * step
end
