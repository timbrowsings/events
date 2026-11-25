modded class MissionServer
{
    override void OnInit()
    {
        super.OnInit();
        TASEventsManager manager = TASEventsManager.GetInstance();
        if (manager)
        {
            manager.RegisterEvent(new TASEventKOTH());
            manager.FinalizeEventRegistration();
        }
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        TASEventsManager manager = TASEventsManager.GetInstance();
        if (manager)
            manager.Update(timeslice);
    }
}
