using System;

using Honey;

namespace Sandbox
{
    public class Player : Entity
    {
        
        void OnCreate()
        {
            Console.WriteLine("Player created.");
        }

        void OnUpdate(float ts)
        {
            Console.WriteLine("Player updated with timestep: " + ts);
        }

    }
}