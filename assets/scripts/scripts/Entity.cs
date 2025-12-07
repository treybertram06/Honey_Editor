using System;

namespace Honey
{
    public class Entity
    {
        public Entity()
        {
            InternalCalls.native_log("Entity instance created.");
        }

        ~Entity()
        {
            InternalCalls.native_log("Entity instance destroyed.");
        }

        void PrintMessage()
        {
            InternalCalls.native_log("Hello from Entity class!");
        }

    }
}