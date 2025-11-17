namespace Honey
{
    public class ScriptableEntity
    {
        public Entity Entity { get; internal set; }

        public virtual void OnCreate() {}
        public virtual void OnUpdate(float deltaTime) {}
        public virtual void OnDestroy() {}
    }
}