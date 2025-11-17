using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Honey
{
    public class Entity
    {
        public ulong ID { get; private set; }

        internal Entity(ulong id)
        {
            ID = id;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void GetPosition(ulong entityID, out Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void SetPosition(ulong entityID, ref Vector3 position);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3
    {
        public float X, Y, Z;
    }
}
