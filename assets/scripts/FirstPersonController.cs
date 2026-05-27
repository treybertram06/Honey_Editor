using System;
using HoneyEngine;

public class FirstPersonController : EntityScript {

    public float PitchLimit       = 89.0f;
    public float MoveSpeed        =  5.0f;
    public float MouseSensitivity =  0.001f;
    public float SprintSpeed      = 25.0f;

    private float _yaw   = 0f;
    private float _pitch = 0f;

    public override void OnCreate() {
        var rot = Entity.Transform.Rotation;
        _yaw   = rot.Y;
        _pitch = rot.X;

        Log.Info("FPS controller initialized");
    }

    public override void OnUpdate(float dt) {
        if (!Input.IsMouseCaptured())
            return;

        var transform = Entity.Transform;

        // --- Mouse look ---
        _yaw   -= Input.GetMouseDeltaX() * MouseSensitivity;
        _pitch -= Input.GetMouseDeltaY() * MouseSensitivity;
        float pitchLimitRad = PitchLimit * (float)(Math.PI / 180.0);
        _pitch  = Math.Clamp(_pitch, -pitchLimitRad, pitchLimitRad);
        transform.Rotation = new Vector3(_pitch, _yaw, 0f);

        // --- Movement ---
        float speed = Input.IsKeyDown(KeyCode.LeftShift) ? SprintSpeed : MoveSpeed;

        float fwdX = -(float)Math.Sin(_yaw);
        float fwdZ = -(float)Math.Cos(_yaw);
        float rgtX =  (float)Math.Cos(_yaw);
        float rgtZ = -(float)Math.Sin(_yaw);

        float moveX = 0f, moveY = 0f, moveZ = 0f;

        if (Input.IsKeyDown(KeyCode.W)) { moveX += fwdX; moveZ += fwdZ; }
        if (Input.IsKeyDown(KeyCode.S)) { moveX -= fwdX; moveZ -= fwdZ; }
        if (Input.IsKeyDown(KeyCode.A)) { moveX -= rgtX; moveZ -= rgtZ; }
        if (Input.IsKeyDown(KeyCode.D)) { moveX += rgtX; moveZ += rgtZ; }

        if (Input.IsKeyDown(KeyCode.Space))       moveY += 1f;
        if (Input.IsKeyDown(KeyCode.LeftControl)) moveY -= 1f;

        float len = (float)Math.Sqrt(moveX * moveX + moveY * moveY + moveZ * moveZ);
        if (len > 0.001f) { moveX /= len; moveY /= len; moveZ /= len; }

        float step = speed * dt;
        var pos = transform.Translation;
        transform.Translation = new Vector3(
            pos.X + moveX * step,
            pos.Y + moveY * step,
            pos.Z + moveZ * step
        );

        // Shooting
        if (Input.IsMouseButtonDown(MouseButton.Left)) {
            var bullet = Scene.InstantiatePrefab("../assets/prefabs/Bulletish.hnp");
            bullet.Rigidbody.SetPosition(new Vector3(transform.Translation.X, transform.Translation.Y - 0.1f, transform.Translation.Z));

            float bFwdX = -(float)Math.Sin(_yaw) * (float)Math.Cos(_pitch);
            float bFwdY =  (float)Math.Sin(_pitch);
            float bFwdZ = -(float)Math.Cos(_yaw) * (float)Math.Cos(_pitch);

            float bulletSpeed = 100f;
            bullet.Rigidbody.ApplyImpulse(new Vector3(bFwdX * bulletSpeed, bFwdY * bulletSpeed, bFwdZ * bulletSpeed));
        }
    }
}