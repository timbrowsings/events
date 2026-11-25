class TAS_KOTHFlagPole extends TerritoryFlag
{
    override void SetActions()
    {
        super.SetActions();
        RemoveAction(ActionRaiseFlag);
        RemoveAction(ActionLowerFlag);
        RemoveAction(ActionDismantlePart);
        RemoveAction(ActionDestroyPart);
    }

    override void FullyBuild()
    {
        if (!GetConstruction()) return;
        array<ConstructionPart> parts = GetConstruction().GetConstructionParts().GetValueArray();
        foreach (ConstructionPart part : parts)
        {
            if (!part) continue;
            ConstructionPart constructionPart = GetConstruction().GetConstructionPart(part.GetPartName());
            if (!constructionPart) continue;
            if (constructionPart.IsBase())
            {
                SetBaseState(true);
                CreateConstructionKit();
            }
            RegisterPartForSync(constructionPart.GetId());
            RegisterActionForSync(constructionPart.GetId(), AT_BUILD_PART);
            SynchronizeBaseState();
            SetPartFromSyncData(constructionPart);
            UpdateNavmesh();
            UpdateVisuals();
        }
        GetConstruction().UpdateVisuals();
        SetAllowDamage(false);
    }

    override void OnPartDismantledServer(notnull Man player, string part_name, int action_id)
    {
    }

    override void OnPartDestroyedServer(Man player, string part_name, int action_id, bool destroyed_by_connected_part = false)
    {
    }
}
