-- buildingController.lua

function OnCreate()
    --Honey.Log("Building controller initialized")
end

function OnUpdate()
    if Honey.Scene.GetOr("gameOver", false) then
        return
    end

    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Building ERROR: Transform missing")
        return
    end
    transform.translation.x = transform.translation.x - 0.6 * dt

    if transform.translation.x <= -10.0 then
        transform.translation.x = 10.0
        --Honey.Log("Building repositioned")
    end

end
