modded class PlayerBase
{
    override void EEKilled(Object killer)
    {
        super.EEKilled(killer);
        if (!GetGame() || !GetGame().IsServer()) return;
        TASEventsManager manager = TASEventsManager.GetInstance();
        if (manager)
            manager.OnKillInZone(this, EntityAI.Cast(killer));
    }
}
