-- pipeSpawner.lua

Properties = {
    spawnInterval = 2.0,
    spawnDisplacement = 15.0,
}

local timer = 0.0

function OnCreate()
    timer = Properties.spawnInterval
    Honey.Log("pipeSpawner initialized")
end

function OnUpdate()
    timer = timer + dt

    if timer < Properties.spawnInterval or Honey.Scene.GetOr("gameOver", false) then
        return
    end

    timer = timer - Properties.spawnInterval

    local centerY = Honey.Random(-1.5, 1.5)
    local gapSize = 2.5

    -- TOP pipe
    local top = Honey.InstantiatePrefab("KinPipe")
    if top then
        local t = top:GetTransform()
        t.scale.y = 10.0
        t.translation.x = Properties.spawnDisplacement
        t.translation.y = centerY + gapSize + (t.scale.y / 2)
    end

    -- BOTTOM pipe
    local bottom = Honey.InstantiatePrefab("KinPipe")
    if bottom then
        local t = bottom:GetTransform()
        t.scale.y = 10.0
        t.translation.x = Properties.spawnDisplacement
        t.translation.y = centerY - gapSize - (t.scale.y / 2)
    end
end