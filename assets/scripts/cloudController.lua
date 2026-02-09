-- cloudController.lua

function OnCreate()
    --Honey.Log("Cloud controller initialized")
end

function OnUpdate()
    if Honey.Scene.GetOr("gameOver", false) then
        return
    end

    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Cloud ERROR: Transform missing")
        return
    end
    transform.translation.x = transform.translation.x - 0.4 * dt

    if transform.translation.x <= -16.0 then
        transform.translation.x = 16.0
        --Honey.Log("Cloud repositioned")
    end

end