-- lightSpawner.lua

Properties = {
    numberOfLights = 10,
    maxDistance = 3.0,
}

function OnCreate()
    local spawner_transform = self:GetTransform()
    if not spawner_transform then
        Honey.Log("lightSpawner ERROR: Transform missing")
        return
    end

    for i = 1, Properties.numberOfLights do
        local object

        if i % 2 == 0 then
            object = Honey.InstantiatePrefab("SpawnedLight")
        else
            object = Honey.InstantiatePrefab("Sphere")
        end

        if object then
            local transform = object:GetTransform()
            transform.translation.x = spawner_transform.translation.x + Honey.Random(-Properties.maxDistance, Properties.maxDistance)
            transform.translation.y = spawner_transform.translation.y + Honey.Random(-Properties.maxDistance, Properties.maxDistance)
            transform.translation.z = spawner_transform.translation.z + Honey.Random(-Properties.maxDistance, Properties.maxDistance)

            local point_light = object:GetComponent("PointLight")
            if point_light then
                point_light.color.x = Honey.Random(0.0, 1.0)
                point_light.color.y = Honey.Random(0.0, 1.0)
                point_light.color.z = Honey.Random(0.0, 1.0)
            end
        end
    end

    Honey.Log("lightSpawner spawned " .. tostring(Properties.numberOfLights) .. " lights")
end

function OnUpdate()
end
