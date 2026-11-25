class CfgPatches
{
    class TASEventKOTH
    {
        units[]={"TAS_KOTHFlagPole"};
        weapons[]={};
        requiredAddons[]={"DZ_Data","DZ_Scripts","DZ_Gear_Camping","JM_CF_Scripts"};
    };
};
class CfgVehicles
{
    class TerritoryFlag;
    class TAS_KOTHFlagPole: TerritoryFlag
    {
        scope=2;
        displayName="KOTH Flagpole";
    };
};
class CfgMods
{
    class TASEventKOTH
    {
        type="mod";
        dependencies[]={"Game","World","Mission"};
        class defs
        {
            class worldScriptModule
            {
                value="";
                files[]={"TASEventKOTH/scripts/4_World"};
            };
            class missionScriptModule
            {
                value="";
                files[]={"TASEventKOTH/scripts/5_Mission"};
            };
        };
    };
};
