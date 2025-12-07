using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using Honey;

namespace Honey
{

    public struct Vector3
    {
        public float x;
        public float y;
        public float z;

        public Vector3(float x, float y, float z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }
    }

    public class CSharpTesting
    {

        CSharpTesting()
        {            
            Console.WriteLine("CSharpTesting instance created.");
            //InternalCalls.native_log("Stan! I'm a butterfly Stan! I need to get some butterfly poon!", 69);

            Vector3 vec = new Vector3(1.5f, 2.3f, 3.1f);
            InternalCalls.native_log_vec(ref vec);
        }

        ~CSharpTesting()
        {
            Console.WriteLine("CSharpTesting instance destroyed.");
        }


        public void PrintMessage()
        {
            Console.WriteLine("STAN IM A BUTTERFLY");
        }

        public void PrintCustomMessage(string message)
        {
            Console.WriteLine($"C# says: {message}");
        }

        public void PrintInteger(int number, int otherNumber)
        {
            Console.WriteLine($"C# received integers: {number}, {otherNumber}");
        }

    }
}