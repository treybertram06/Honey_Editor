-- bushController.lua

function OnCreate()
    --Honey.Log("Bush controller initialized")
end

function OnUpdate()
    if Honey.Scene.GetOr("gameOver", false) then
        return
    end

    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Bush ERROR: Transform missing")
        return
    end
    transform.translation.x = transform.translation.x - 0.8 * dt

    if transform.translation.x <= -10.0 then
        transform.translation.x = 10.0
        --Honey.Log("Bush repositioned")
    end

end
