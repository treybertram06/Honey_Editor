-- pipeSpawner.lua

local timer = 0.0
local spawnInterval = 2.0

function OnCreate(self)
    Honey.Log("pipeSpawner initialized")
end

function OnUpdate(self, dt)
    timer = timer + dt

    if timer >= spawnInterval then
        timer = timer - spawnInterval

        -- Random gap center
        local centerY = Honey.Random(-1.5, 1.5)
        local gapSize = 2.5

        -- TOP pipe
        local top = Honey.InstantiatePrefab("KinPipe")

        local t2 = top:GetTransform()

        t2.scale.y = 10.0

        t2.translation.x = 10.0
        t2.translation.y = centerY + gapSize + (t2.scale.y / 2)

        -- BOTTOM pipe
        local bottom = Honey.InstantiatePrefab("KinPipe")

        local t = bottom:GetTransform()

        t.scale.y = 10.0

        t.translation.x = 10.0
        t.translation.y = centerY - gapSize - (t.scale.y / 2)



    end
end
