class TASEventLootPresetBuild
{
    string magazine = "";
    ref array<string> attachments;

    void TASEventLootPresetBuild()
    {
        attachments = new array<string>;
    }

    void EnsureArrays()
    {
        if (!attachments) attachments = new array<string>;
    }
}

class TASEventLootItem
{
    string classname = "";
    float chance = 1.0;
    int min = 1;
    int max = 1;
    float quantityMin = -1.0;
    float quantityMax = -1.0;

    // Weighted build selection. Key is "bad" / "medium" / "best", value is a
    // fraction; the three together must sum to 1.0. One draw picks a single
    // tier so a gun can never spawn two builds at once.
    ref map<string, float> tiers;
    // The concrete build per tier, baked by the website editor.
    ref map<string, ref TASEventLootPresetBuild> presets;

    void EnsureArrays()
    {
        if (!tiers) tiers = new map<string, float>;
        if (!presets) presets = new map<string, ref TASEventLootPresetBuild>;
    }
}

// A named group of items with its own draw count, so one tier can promise
// "1-2 weapons and 2-3 medical" without the two pools competing for one list.
// The nesting matches TASKeyroomUpgradeKeyPool / upgradeKeysByTier in the
// keyroom mod, which already loads from JSON on this build: an array of
// objects, each holding a ref array<string>.
class TASEventLootPool
{
    string name = "";
    int min = 0;
    int max = 0;
    ref array<ref TASEventLootItem> items;

    void TASEventLootPool()
    {
        items = new array<ref TASEventLootItem>;
    }

    void EnsureArrays()
    {
        if (!items) items = new array<ref TASEventLootItem>;
    }
}

class TASEventLootTier
{
    string tier = "";
    string chestClassname = "TAS_Keyroom_Military1_Sand";
    int randomPoolCountMin = 3;
    int randomPoolCountMax = 5;
    // Guaranteed item count per crate. 0 and 0 means "no floor, no ceiling"
    // (the old behaviour); any positive value is enforced after the roll.
    int itemCountMin = 0;
    int itemCountMax = 0;
    ref array<string> randomPool;
    // Named pools, each rolled independently. Absent from the JSON means an
    // empty array, which leaves every existing tier behaving exactly as before.
    ref array<ref TASEventLootPool> lootPools;
    ref array<ref TASEventLootItem> items;

    void EnsureArrays()
    {
        if (!randomPool) randomPool = new array<string>;
        if (!lootPools) lootPools = new array<ref TASEventLootPool>;
        if (!items) items = new array<ref TASEventLootItem>;
    }
}

class TASEventLootchestConfig
{
    ref array<ref TASEventLootTier> tiers;

    void EnsureArrays()
    {
        if (!tiers) tiers = new array<ref TASEventLootTier>;
    }

    TASEventLootTier GetTier(string tier)
    {
        EnsureArrays();
        string wanted = TrimString(tier);
        wanted.ToUpper();
        foreach (TASEventLootTier t : tiers)
        {
            if (!t) continue;
            string tierName = TrimString(t.tier);
            tierName.ToUpper();
            if (tierName == wanted) return t;
        }
        return null;
    }

    private string TrimString(string s)
    {
        int start = 0;
        int end = s.Length();
        while (start < end && IsWhitespaceChar(s.Get(start))) start++;
        while (end > start && IsWhitespaceChar(s.Get(end - 1))) end--;
        if (start >= end) return "";
        return s.Substring(start, end - start);
    }

    private bool IsWhitespaceChar(string ch)
    {
        return ch == " " || ch == "\t" || ch == "\r" || ch == "\n";
    }
}

class TASEventLootchest
{
    private static ref TASEventLootchest m_Instance;
    private ref map<string, ref TASEventLootchestConfig> m_Configs;
    private ref array<EntityAI> m_ActiveChests;
    private ref array<float> m_ChestTimers;

    private const int LOOTCHEST_LIFETIME_MINUTES = 20;
    private const float LOOTCHEST_LIFETIME_SECONDS = 1200.0;
    private const string DEFAULT_LOOTCHEST = "TAS_Keyroom_Military1_Sand";

    void TASEventLootchest()
    {
        m_Configs = new map<string, ref TASEventLootchestConfig>;
        m_ActiveChests = new array<EntityAI>;
        m_ChestTimers = new array<float>;
    }

    static TASEventLootchest GetInstance()
    {
        if (!m_Instance) m_Instance = new TASEventLootchest();
        return m_Instance;
    }

    void EnsureConfigs(array<string> eventTypes)
    {
        foreach (string eventType : eventTypes)
        {
            LoadConfig(eventType);
        }
    }

    void Update(float deltaT)
    {
        if (!GetGame() || !m_ActiveChests || !m_ChestTimers) return;
        while (m_ChestTimers.Count() > m_ActiveChests.Count())
            m_ChestTimers.Remove(m_ChestTimers.Count() - 1);
        while (m_ActiveChests.Count() > m_ChestTimers.Count())
            m_ActiveChests.Remove(m_ActiveChests.Count() - 1);
        for (int i = m_ActiveChests.Count() - 1; i >= 0; i--)
        {
            float timeLeft = m_ChestTimers.Get(i) - deltaT;
            if (timeLeft <= 0)
            {
                EntityAI chest = m_ActiveChests.Get(i);
                if (chest)
                {
                    GetGame().ObjectDelete(chest);
                    TASEventsManager.GetInstance().LogAction("LOOTCHEST DELETED | after " + LOOTCHEST_LIFETIME_MINUTES.ToString() + " minutes");
                }
                m_ActiveChests.Remove(i);
                m_ChestTimers.Remove(i);
            }
            else
            {
                m_ChestTimers.Set(i, timeLeft);
            }
        }
    }

    private void LoadConfig(string eventType)
    {
        if (m_Configs.Contains(eventType)) return;

        TASEventLootchestConfig cfg = new TASEventLootchestConfig();
        string lootPath = "$profile:TASEvents/" + eventType + "/" + eventType + "Lootchest.json";
        if (FileExist(lootPath))
        {
            JsonFileLoader<TASEventLootchestConfig>.JsonLoadFile(lootPath, cfg);
        }
        else
        {
            TASEventsManager.GetInstance().LogAction("WARNING: lootchest config not found at " + lootPath + " - lootchests will spawn empty");
        }
        cfg.EnsureArrays();
        m_Configs.Insert(eventType, cfg);
    }

    void SpawnLootchest(string eventType, string tier, vector position, vector orientation)
    {
        LoadConfig(eventType);

        TASEventLootTier tierCfg = null;
        if (m_Configs.Contains(eventType))
        {
            TASEventLootchestConfig lootCfg = m_Configs.Get(eventType);
            if (lootCfg) tierCfg = lootCfg.GetTier(tier);
        }

        string chestClass = DEFAULT_LOOTCHEST;
        if (tierCfg && tierCfg.chestClassname != "")
            chestClass = tierCfg.chestClassname;

        EntityAI chest = SpawnContainer(chestClass, position, orientation);
        if (!chest)
        {
            TASEventsManager.GetInstance().LogEventAction(eventType, "LOOTCHEST SPAWN FAILED | tier: " + tier + " | classname: " + chestClass + " | position: " + position.ToString());
            return;
        }

        if (tierCfg)
        {
            FillLootchest(chest, tierCfg);
        }
        else
        {
            TASEventsManager.GetInstance().LogEventAction(eventType, "LOOTCHEST WARNING | no loot section found for tier: " + tier + " - chest spawned empty");
        }

        FinalizeLootchest(chest);

        m_ActiveChests.Insert(chest);
        m_ChestTimers.Insert(LOOTCHEST_LIFETIME_SECONDS);

        TASEventsManager.GetInstance().LogEventAction(eventType, "LOOTCHEST SPAWNED | tier: " + tier + " | chest: " + chestClass + " | position: " + position.ToString() + " | auto-delete in " + LOOTCHEST_LIFETIME_MINUTES.ToString() + " minutes");
    }

    private EntityAI SpawnContainer(string classname, vector position, vector orientation)
    {
        if (classname == "")
            return null;

        Object object = GetGame().CreateObjectEx(classname, position, ECE_SETUP | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH);

        if (!object)
            return null;

        object.SetPosition(position);
        object.SetOrientation(orientation);

        EntityAI entity = EntityAI.Cast(object);

        if (entity)
        {
            entity.SetLifetime(LOOTCHEST_LIFETIME_SECONDS);
            PrepareLootchestForFilling(entity);
        }

        return entity;
    }

    private void PrepareLootchestForFilling(EntityAI container)
    {
        if (!container)
            return;

        if (container.GetInventory())
        {
            container.GetInventory().UnlockInventory(HIDE_INV_FROM_SCRIPT);
            container.GetInventory().UnlockInventory(LOCK_FROM_SCRIPT);
        }

        TAS_Keyroom_Military1_Sand lootchest = TAS_Keyroom_Military1_Sand.Cast(container);

        if (lootchest)
            lootchest.TAS_BeginKeyroomRewardFill();
    }

    private void FinalizeLootchest(EntityAI container)
    {
        if (!container)
            return;

        TAS_Keyroom_Military1_Sand lootchest = TAS_Keyroom_Military1_Sand.Cast(container);

        if (lootchest)
        {
            lootchest.TAS_FinishKeyroomRewardSetup();
            return;
        }

        container.SetSynchDirty();
    }

    private const string TIER_BAD = "bad";
    private const string TIER_MEDIUM = "medium";
    private const string TIER_BEST = "best";

    // One weighted draw. Returns "" when the item has no tier map, so plain
    // loot entries keep behaving exactly as before.
    private string RollWeightedTier(TASEventLootItem lootItem)
    {
        if (!lootItem) return "";
        lootItem.EnsureArrays();
        if (lootItem.tiers.Count() == 0) return "";

        float roll = Math.RandomFloat01();
        float acc = 0.0;

        // Fixed order: map iteration order is not deterministic and the roll
        // has to map to the same tier for the same seed. A dynamic array, not a
        // fixed-size C array -- Enforce does not support the latter.
        ref array<string> order = new array<string>;
        order.Insert(TIER_BAD);
        order.Insert(TIER_MEDIUM);
        order.Insert(TIER_BEST);

        for (int i = 0; i < order.Count(); i++)
        {
            string key = order.Get(i);
            if (!lootItem.tiers.Contains(key)) continue;
            acc += lootItem.tiers.Get(key);
            if (roll < acc) return key;
        }

        // Float edge case at exactly 1.0: fall back to the last weighted tier.
        string lastTier = "";
        for (int j = 0; j < order.Count(); j++)
        {
            string k = order.Get(j);
            if (lootItem.tiers.Contains(k) && lootItem.tiers.Get(k) > 0.0)
                lastTier = k;
        }
        return lastTier;
    }

    private void ApplyWeaponPreset(EntityAI weapon, TASEventLootItem lootItem, string tierKey)
    {
        if (!weapon) return;
        if (tierKey == "") return;
        if (!lootItem) return;

        lootItem.EnsureArrays();
        if (!lootItem.presets.Contains(tierKey)) return;

        TASEventLootPresetBuild build = lootItem.presets.Get(tierKey);
        if (!build) return;

        build.EnsureArrays();

        if (build.magazine != "")
        {
            EntityAI mag = weapon.GetInventory().CreateAttachment(build.magazine);
            if (!mag)
                TASEventsManager.GetInstance().LogAction("PRESET WARNING | magazine failed | " + build.magazine + " on " + weapon.GetType());
        }

        int failed = 0;

        foreach (string cls : build.attachments)
        {
            if (cls == "") continue;
            if (!weapon.GetInventory().CreateAttachment(cls)) failed++;
        }

        if (failed > 0)
            TASEventsManager.GetInstance().LogAction("PRESET WARNING | " + failed.ToString() + " attachment(s) failed on " + weapon.GetType() + " | tier " + tierKey);

        weapon.SetSynchDirty();
    }

    private void FillLootchest(EntityAI chest, TASEventLootTier tierCfg)
    {
        if (!chest || !tierCfg) return;

        tierCfg.EnsureArrays();

        // How many items this crate already holds; everything below is bounded
        // by the tier's itemCountMin/itemCountMax so a crate can never come up
        // empty and can never be stuffed past its ceiling.
        int itemsBefore = CountContainerItems(chest);

        if (tierCfg.randomPool.Count() > 0)
        {
            int countMin = tierCfg.randomPoolCountMin;
            int countMax = tierCfg.randomPoolCountMax;

            if (countMin < 0) countMin = 0;
            if (countMax < countMin) countMax = countMin;
            if (countMin > tierCfg.randomPool.Count()) countMin = tierCfg.randomPool.Count();
            if (countMax > tierCfg.randomPool.Count()) countMax = tierCfg.randomPool.Count();

            int pickCount = Math.RandomInt(countMin, countMax + 1);

            // the random pool shares the crate ceiling with the rolled items
            int poolRoom = RoomInCrate(chest, tierCfg);
            if (pickCount > poolRoom) pickCount = poolRoom;

            array<string> poolCopy = new array<string>;
            poolCopy.Copy(tierCfg.randomPool);

            for (int p = 0; p < pickCount && poolCopy.Count() > 0 && RoomInCrate(chest, tierCfg) > 0; p++)
            {
                int idx = Math.RandomInt(0, poolCopy.Count());
                string poolClassname = poolCopy.Get(idx);
                poolCopy.Remove(idx);

                if (poolClassname == "") continue;

                AddItemToChest(chest, poolClassname);
            }
        }

        // Named pools. Each pool is its own draw, so "1-2 weapons and 2-3
        // medical" pays out both instead of splitting one shared quota. Picks
        // are without replacement inside a pool, and every pool still shares
        // the crate ceiling, so pools alone can never overfill a chest.
        for (int lp = 0; lp < tierCfg.lootPools.Count(); lp++)
        {
            TASEventLootPool poolCfg = tierCfg.lootPools.Get(lp);

            if (!poolCfg) continue;

            poolCfg.EnsureArrays();

            if (poolCfg.items.Count() == 0) continue;

            int poolMin = poolCfg.min;
            int poolMax = poolCfg.max;

            if (poolMin < 0) poolMin = 0;
            if (poolMax < poolMin) poolMax = poolMin;
            if (poolMin > poolCfg.items.Count()) poolMin = poolCfg.items.Count();
            if (poolMax > poolCfg.items.Count()) poolMax = poolCfg.items.Count();

            int poolPicks = Math.RandomInt(poolMin, poolMax + 1);

            // Named "poolSpace", not "poolRoom": FillLootchest already declares
            // poolRoom for the randomPool block, and a repeat declaration in the
            // same method is not worth risking on a build I cannot compile here.
            int poolSpace = RoomInCrate(chest, tierCfg);
            if (poolPicks > poolSpace) poolPicks = poolSpace;

            array<ref TASEventLootItem> poolBag = new array<ref TASEventLootItem>;
            for (int pb = 0; pb < poolCfg.items.Count(); pb++)
            {
                TASEventLootItem pEntry = poolCfg.items.Get(pb);
                if (pEntry) poolBag.Insert(pEntry);
            }

            for (int pi = 0; pi < poolPicks && poolBag.Count() > 0 && RoomInCrate(chest, tierCfg) > 0; pi++)
            {
                int poolIdx = Math.RandomInt(0, poolBag.Count());
                TASEventLootItem poolItem = poolBag.Get(poolIdx);
                poolBag.Remove(poolIdx);

                if (!poolItem || poolItem.classname == "") continue;

                EntityAI spawnedPoolLoot = AddItemToChest(chest, poolItem.classname);
                if (spawnedPoolLoot && poolItem.quantityMin >= 0.0 && poolItem.quantityMax >= 0.0)
                    ApplyQuantity(spawnedPoolLoot, poolItem.quantityMin, poolItem.quantityMax);
                if (spawnedPoolLoot)
                {
                    string poolPickedTier = RollWeightedTier(poolItem);
                    if (poolPickedTier != "")
                        ApplyWeaponPreset(spawnedPoolLoot, poolItem, poolPickedTier);
                }
            }
        }

        for (int i = 0; i < tierCfg.items.Count(); i++)
        {
            TASEventLootItem lootItem = tierCfg.items.Get(i);

            if (!lootItem)
                continue;

            if (lootItem.classname == "")
                continue;

            float roll = Math.RandomFloat01();

            if (roll > lootItem.chance)
                continue;

            int minCount = lootItem.min;
            int maxCount = lootItem.max;

            if (minCount < 1)
                minCount = 1;

            if (maxCount < minCount)
                maxCount = minCount;

            int amount = Math.RandomInt(minCount, maxCount + 1);

            for (int j = 0; j < amount; j++)
            {
                if (RoomInCrate(chest, tierCfg) <= 0)
                    break;      // ceiling reached: stop adding, do not overfill

                EntityAI spawnedLoot = AddItemToChest(chest, lootItem.classname);

                if (spawnedLoot && lootItem.quantityMin >= 0.0 && lootItem.quantityMax >= 0.0)
                    ApplyQuantity(spawnedLoot, lootItem.quantityMin, lootItem.quantityMax);

                // Weighted build: one draw decides bad/medium/best, then every
                // part of that build is attached. No-op for static loot.
                if (spawnedLoot)
                {
                    string pickedTier = RollWeightedTier(lootItem);
                    if (pickedTier != "")
                        ApplyWeaponPreset(spawnedLoot, lootItem, pickedTier);
                }
            }
        }

        // Floor: if every chance roll failed the crate would be empty, so keep
        // taking entries until the promised minimum is met.
        EnsureMinimumLoot(chest, tierCfg, itemsBefore);
    }

    // How many more items may still be added before the tier ceiling is hit.
    // A ceiling of 0 (or lower than the floor) means unlimited.
    private int RoomInCrate(EntityAI chest, TASEventLootTier tierCfg)
    {
        if (!chest) return 0;
        int maxItems = tierCfg.itemCountMax;
        if (tierCfg.itemCountMin > maxItems)
            maxItems = tierCfg.itemCountMin;
        if (maxItems <= 0)
            return 9999;        // no ceiling configured
        return maxItems - CountContainerItems(chest);
    }

    private int CountContainerItems(EntityAI container)
    {
        if (!container) return 0;
        GameInventory inv = container.GetInventory();
        if (!inv) return 0;
        return inv.GetCargo().GetItemCount();
    }

    // Top the crate up to itemCountMin using any configured loot source.
    private void EnsureMinimumLoot(EntityAI chest, TASEventLootTier tierCfg, int itemsBefore)
    {
        if (!chest) return;
        int wanted = tierCfg.itemCountMin;
        if (wanted <= 0) return;                 // no floor configured

        bool hasNamedPoolItems = false;
        foreach (TASEventLootPool namedPool : tierCfg.lootPools)
        {
            if (namedPool && namedPool.items && namedPool.items.Count() > 0)
            {
                hasNamedPoolItems = true;
                break;
            }
        }
        if (tierCfg.items.Count() == 0 && tierCfg.randomPool.Count() == 0 && !hasNamedPoolItems)
            return;                              // nothing to guarantee

        int guard = 0;
        while (CountContainerItems(chest) - itemsBefore < wanted && guard < 200 && RoomInCrate(chest, tierCfg) > 0)
        {
            guard++;

            // Prefer an item from the ordinary list when its chance succeeds.
            bool placed = false;
            for (int i = 0; i < tierCfg.items.Count() && !placed; i++)
            {
                TASEventLootItem lootItem = tierCfg.items.Get(i);
                if (!lootItem || lootItem.classname == "" || lootItem.chance <= 0.0) continue;

                if (Math.RandomFloat01() <= lootItem.chance)
                {
                    EntityAI spawned = AddItemToChest(chest, lootItem.classname);
                    if (spawned)
                    {
                        if (lootItem.quantityMin >= 0.0 && lootItem.quantityMax >= 0.0)
                            ApplyQuantity(spawned, lootItem.quantityMin, lootItem.quantityMax);
                        string pickedTier = RollWeightedTier(lootItem);
                        if (pickedTier != "")
                            ApplyWeaponPreset(spawned, lootItem, pickedTier);
                        placed = true;
                    }
                }
            }

            // The floor must also work when a tier is made up of named pools.
            if (!placed && hasNamedPoolItems)
            {
                array<ref TASEventLootItem> namedFallbacks = new array<ref TASEventLootItem>;
                foreach (TASEventLootPool namedPool : tierCfg.lootPools)
                {
                    if (!namedPool || !namedPool.items) continue;
                    foreach (TASEventLootItem poolItem : namedPool.items)
                    {
                        if (poolItem && poolItem.classname != "")
                            namedFallbacks.Insert(poolItem);
                    }
                }

                while (!placed && namedFallbacks.Count() > 0)
                {
                    int fallbackIndex = Math.RandomInt(0, namedFallbacks.Count());
                    TASEventLootItem poolFallback = namedFallbacks.Get(fallbackIndex);
                    namedFallbacks.Remove(fallbackIndex);
                    EntityAI poolSpawned = AddItemToChest(chest, poolFallback.classname);
                    if (!poolSpawned) continue;
                    if (poolFallback.quantityMin >= 0.0 && poolFallback.quantityMax >= 0.0)
                        ApplyQuantity(poolSpawned, poolFallback.quantityMin, poolFallback.quantityMax);
                    string poolTier = RollWeightedTier(poolFallback);
                    if (poolTier != "")
                        ApplyWeaponPreset(poolSpawned, poolFallback, poolTier);
                    placed = true;
                }
            }

            // Then use the legacy random pool, followed by the most likely
            // ordinary item as the final fallback.
            if (!placed && tierCfg.randomPool.Count() > 0)
            {
                string pick = tierCfg.randomPool.Get(Math.RandomInt(0, tierCfg.randomPool.Count()));
                if (pick != "")
                    placed = AddItemToChest(chest, pick) != null;
            }

            if (!placed)
            {
                TASEventLootItem fallback = MostLikelyItem(tierCfg);
                if (!fallback) break;
                EntityAI forced = AddItemToChest(chest, fallback.classname);
                if (!forced) break;              // nothing fits any more
                if (fallback.quantityMin >= 0.0 && fallback.quantityMax >= 0.0)
                    ApplyQuantity(forced, fallback.quantityMin, fallback.quantityMax);
                string forcedTier = RollWeightedTier(fallback);
                if (forcedTier != "")
                    ApplyWeaponPreset(forced, fallback, forcedTier);
            }
        }
    }

    private TASEventLootItem MostLikelyItem(TASEventLootTier tierCfg)
    {
        TASEventLootItem best = null;
        for (int i = 0; i < tierCfg.items.Count(); i++)
        {
            TASEventLootItem candidate = tierCfg.items.Get(i);
            if (!candidate || candidate.classname == "") continue;
            if (!best || candidate.chance > best.chance)
                best = candidate;
        }
        return best;
    }

    private EntityAI AddItemToChest(EntityAI container, string itemClassname)
    {
        if (!container)
            return null;

        if (itemClassname == "")
            return null;

        PrepareLootchestForFilling(container);

        if (!container.GetInventory())
        {
            TASEventsManager.GetInstance().LogAction("LOOTCHEST LOOT ERROR | container has no inventory component | container: " + container.GetType());
            return null;
        }

        EntityAI item = EntityAI.Cast(container.GetInventory().CreateInInventory(itemClassname));

        if (!item)
            TASEventsManager.GetInstance().LogAction("LOOTCHEST LOOT FAILED | CreateInInventory failed | item: " + itemClassname + " | container: " + container.GetType());

        return item;
    }

    private void ApplyQuantity(EntityAI item, float quantityMin, float quantityMax)
    {
        if (!item)
            return;

        if (quantityMax < quantityMin)
            quantityMax = quantityMin;

        ItemBase itemBase = ItemBase.Cast(item);

        if (!itemBase)
            return;

        float quantity = Math.RandomFloat(quantityMin, quantityMax);
        itemBase.SetQuantity(quantity);
    }
}