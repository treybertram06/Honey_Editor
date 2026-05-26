using System;
using HoneyEngine;

public class GlueTest : EntityScript {

    private float _timer = 0f;

    public override void OnCreate() {
        Log.Info("GlueTest.OnCreate fired — log glue OK");

        var pos = Entity.Transform.Translation;
        Log.Info($"Initial translation: ({pos.X:F2}, {pos.Y:F2}, {pos.Z:F2}) — transform read glue OK");
    }

    public override void OnUpdate(float dt) {
        _timer += dt;

        // Write a bouncing Y position so you can see it move in the editor
        float y = (float)Math.Sin(_timer * 2f);
        var transform = Entity.Transform;
        var pos = transform.Translation;
        transform.Translation = new Vector3(pos.X, y, pos.Z);

        // Log once per second so the console is not flooded
        if (_timer % 1f < dt) {
            Log.Info($"GlueTest alive — t={_timer:F1}s  pos.Y={y:F2} — transform write glue OK");

            bool space = Input.IsKeyDown(KeyCode.Space);
            Log.Info($"Space held: {space} — input glue OK");
        }
    }

    public override void OnDestroy() {
        Log.Info("GlueTest.OnDestroy fired");
    }
}