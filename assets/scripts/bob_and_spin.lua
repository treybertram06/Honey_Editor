-- bob_and_spin.lua

function OnCreate()
    time = 0.0
end


function OnUpdate()
    local transform = self:GetTransform()
    if not transform then
        Honey.Log("Weaving all round ERROR: Transform missing")
        return
    end
    time = time + dt
    transform.translation.y = 1.0 + 0.5 * math.sin(time * 1.0)
    transform.rotation.x = transform.rotation.x + math.sin(time) * 0.7
    transform.rotation.y = transform.rotation.y + math.sin(time) * 0.3
    transform.rotation.z = transform.rotation.z + math.sin(time)


end
