-- musicController.lua

local audioSrc = nil

function OnCreate()
    audioSrc = self:GetComponent("AudioSource")

    audioSrc.volume = 1.0
    audioSrc.pitch = 1.0
    audioSrc.loop = true
    audioSrc:play()

    Honey.Log("Music Controller Initialized.")
end

function OnUpdate()

    if Honey.IsKeyPressed(Key.Space) then
        audioSrc:play()
        Honey.Log("Music Playing (Hopefully).")
    end

end


