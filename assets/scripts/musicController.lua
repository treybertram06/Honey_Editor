-- musicController.lua

local audioSrc = nil
local playing = false
local time = 0.0

function OnCreate()
    audioSrc = self:GetComponent("AudioSource")

    audioSrc.volume = 1.0
    audioSrc.pitch = 1.0
    audioSrc.loop = false

    Honey.Log("Music Controller Initialized.")
end

function OnUpdate()
    if Honey.IsKeyPressed(Key.Space) and not playing then
        audioSrc:play()
        Honey.Log("Music Playing.")
        playing = true
    end

    if playing then
        time = time + dt
    end

    if time >= 42.0 then
        audioSrc:play_at(31.0)
        time = 31.0
        --Honey.Log("Music Looping.")
    end

end


