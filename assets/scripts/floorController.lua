-- floorController.lua

function OnCreate()
    Honey.Log("Floor controller initialized")
end

function OnUpdate()
    if Honey.Scene.GetOr("gameOver", false) then
        return
    end

    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Floor ERROR: Transform missing")
        return
    end
    transform.translation.x = transform.translation.x - 3.0 * dt

    if transform.translation.x <= -15.0 then
        transform.translation.x = 15.0
        Honey.Log("Floor repositioned")
    end

end