using System;
using HoneyEngine;

public class SceneLoadTest : EntityScript {

    private bool triggered = false;
    private float start_time = 0f;

    public override void OnCreate() {
        start_time = Time.Elapsed;
    }

    public override void OnUpdate(float dt) {

        if (Time.Elapsed % 1f < dt) {
            Log.Info($"Waiting... {Time.Elapsed:F0}s");
        }

        if (Time.Elapsed % 5f < dt && !triggered) {
            triggered = true;
            Scene.Load("scenes/sponza.hns");
        }
    }

    public override void OnDestroy() {}
}