using System;
using HoneyEngine;

public class BobAndSpin : EntityScript {

    private float _timer = 0f;

    public override void OnCreate() {
        Log.Info($"Bobbing... AND SPINNING???");
    }

    public override void OnUpdate(float dt) {
        _timer += dt;

        // Write a bouncing Y position so you can see it move in the editor
        var transform = Entity.Transform;

        var pos = transform.Translation;
        float pos_y = 1.0f + 0.5f * (float)Math.Sin(_timer);
        transform.Translation = new Vector3(pos.X, pos_y, pos.Z);

        var rot = transform.Rotation;
        float spinSpeed = 1.5f; // rad/s
        transform.Rotation = new Vector3(
            rot.X + spinSpeed * 0.7f * dt,
            rot.Y + spinSpeed * 0.3f * dt,
            rot.Z + spinSpeed * dt
        );
    }

    public override void OnDestroy() {
        Log.Info("GlueTest.OnDestroy fired");
    }
}