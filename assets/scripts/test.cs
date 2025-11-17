using System;

namespace Honey
{
    public class TestScript : ScriptableEntity
    {
        public override void OnCreate()
        {
            Console.WriteLine("TestScript created!");
        }

        public override void OnUpdate(float deltaTime)
        {
            Console.WriteLine("Beginning Update with deltaTime: " + deltaTime);
            Vector3 pos;
            Entity.GetPosition(Entity.ID, out pos);
            Console.WriteLine("Successfully got position: " + pos);
            pos.X += 1.0f * deltaTime;
            Entity.SetPosition(Entity.ID, ref pos);
            Console.WriteLine("Successfully set position to: " + pos);
        }

        public override void OnDestroy()
        {
            Console.WriteLine("TestScript destroyed!");
        }
    }
}
