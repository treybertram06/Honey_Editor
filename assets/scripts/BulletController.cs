using System;
using HoneyEngine;

public class BulletController : EntityScript {

    private float _timer = 0f;

    public override void OnCreate() {}

    public override void OnUpdate(float dt) {
        _timer += dt;

        if (_timer > 10.0f) {
            Entity.Destroy();
        }
    }
}