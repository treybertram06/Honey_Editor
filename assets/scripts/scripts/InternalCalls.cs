using System;
using System.Runtime.CompilerServices;

namespace Honey
{
    public static class InternalCalls
    {
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern public static void native_log(string text);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        extern public static void native_log_vec(ref Vector3 param);
    }
}