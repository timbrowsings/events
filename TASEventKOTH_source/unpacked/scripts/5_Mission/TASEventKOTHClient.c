modded class MissionGameplay
{
    override void OnInit()
    {
        super.OnInit();
        GetRPCManager().AddRPC("TASEvents", "ShowNotify", this, SingleplayerExecutionType.Client);
    }

    void ShowNotify(CallType type, ParamsReadContext ctx, PlayerIdentity sender, Object target)
    {
        Param1<string> notify;
        if (!ctx.Read(notify)) return;
        if (!GetGame()) return;
        GetGame().Chat(notify.param1, "colorAction");
    }
}
