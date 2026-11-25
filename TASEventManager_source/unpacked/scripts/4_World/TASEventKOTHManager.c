class TASEventKOTHLocation
{
    vector pos;
    string name;
    string tier;
    void TASEventKOTHLocation(vector p, string n, string t) { pos = p; name = n; tier = t; }
}

class TASEventKOTH : TASEventBase
{
    private ref array<ref TASEventKOTHLocation> m_KOTHLocations;
    private static int m_LastLocationIndex = -1;
    private TASEventKOTHLocation m_ActiveLocation;

    private float m_Points = 0;
    private float m_Timer = 0;
    private float m_TotalLifetime = 0;
    private float m_TimeoutGraceTimer = 0.0;
    private int m_LastDisplayedPct = -1;
    private bool m_WasCaptured = false;
    private bool m_SmokeTriggered = false;
    private bool m_ToxicZoneSpawned = false;
    private bool m_ResumeFlagpolePending = false;
    private float m_ResumeFlagpolePollTimer = 0;
    private float m_ResumeFlagpoleWaitTimer = 0;

    private TerritoryFlag m_EventFlagpole;
    private TerritoryFlag m_PendingFlagpole;
    private ref array<SmokeGrenadeBase> m_SmokeGrenades;
    private int m_PendingMarkerUID = -1;
    private float m_PendingCleanupTimer = 0;
    private int m_LastFlagPercent = -1;
    private ref map<PlayerBase, bool> m_InMissionArea;
    private ref map<PlayerBase, bool> m_InCaptureArea;
    private ref map<PlayerBase, float> m_LastMissionZoneNotice;
    private ref map<PlayerBase, float> m_LastCaptureZoneNotice;
    private bool m_ToxicZoneRetryPending = false;
    private float m_ToxicZoneRetryTimer = 0.0;
    private int m_ToxicZoneRetryCount = 0;

    #ifdef LBmaster_Groups
    private LBServerMarker m_ActiveMarker;
    #endif

    private const float MAX_POINTS = 600.0;
    private const float CAPTURE_RAD = 25.0;
    private const float MISSION_RAD = 100.0;
    private const float MAX_TIME = 1800.0;
    private const float TIMEOUT_CAPTURE_GRACE_SECONDS = 60.0;

    private const float FLAG_HEADING = 180.0;
    private const float LOOTCHEST_OFFSET = 2.5;

    private const float CLEANUP_DELAY_SECONDS = 300.0;
    private const float RESUME_POS_MATCH_RAD = 5.0;
    private const float RESUME_POS_WARN_RAD = 100.0;
    private const float ZONE_NOTICE_COOLDOWN_SECONDS = 10.0;

    private const int TOXIC_ZONE_MAX_RETRIES = 5;
    private const float TOXIC_ZONE_RETRY_INTERVAL_SECONDS = 15.0;

    private const float TOXIC_RADIUS         = 100.0;
    private const float TOXIC_POS_HEIGHT     = 25.0;
    private const float TOXIC_NEG_HEIGHT     = 10.0;
    private const int   TOXIC_INNER_RINGS    = 1;
    private const int   TOXIC_INNER_SPACING  = 35;
    private const bool  TOXIC_OUTER_TOGGLE   = true;
    private const int   TOXIC_OUTER_SPACING  = 20;
    private const int   TOXIC_OUTER_OFFSET   = -5;
    private const int   TOXIC_V_LAYERS       = 0;
    private const int   TOXIC_V_OFFSET       = 10;

    void TASEventKOTH()
    {
        m_EventName = "KOTH";
        m_InMissionArea = new map<PlayerBase, bool>;
        m_InCaptureArea = new map<PlayerBase, bool>;
        m_LastMissionZoneNotice = new map<PlayerBase, float>;
        m_LastCaptureZoneNotice = new map<PlayerBase, float>;
        m_KOTHLocations = new array<ref TASEventKOTHLocation>;
        m_SmokeGrenades = new array<SmokeGrenadeBase>;
        LoadKOTHLocations();
    }

    void LoadKOTHLocations()
    {
        string path = "$profile:TASEvents/KOTH/KOTHLocations.txt";
        FileHandle fh = OpenFile(path, FileMode.READ);
        if (fh)
        {
            string line;
            while (FGets(fh, line) > 0)
            {
                line = TrimString(line);
                if (line == "" || !line.Contains("<")) continue;

                int sIdx = line.IndexOf("<");
                int eIdx = line.IndexOf(">");
                if (eIdx <= sIdx + 1) continue;

                string tierPart = SanitizeTierToken(line.Substring(0, sIdx));

                vector pos = line.Substring(sIdx + 1, eIdx - sIdx - 1).ToVector();

                string locName = TrimString(line.Substring(eIdx + 1, line.Length() - eIdx - 1));

                if (tierPart == "")
                {
                    tierPart = "T1";
                    TASEventsManager.GetInstance().LogAction("WARNING: KOTH location '" + locName + "' has no tier - defaulting to T1.");
                }
                else if (!IsKnownTier(tierPart))
                {
                    TASEventsManager.GetInstance().LogAction("WARNING: KOTH location '" + locName + "' uses unknown tier '" + tierPart + "' (expected T1, T2, T3, T4 or CA).");
                }

                m_KOTHLocations.Insert(new TASEventKOTHLocation(pos, locName, tierPart));
            }
            CloseFile(fh);
        }
        else
        {
            TASEventsManager.GetInstance().LogAction("WARNING: KOTHLocations.txt not found at " + path + " - KOTH cannot spawn without locations.");
        }
    }

    bool IsKnownTier(string tier)
    {
        return tier == "T1" || tier == "T2" || tier == "T3" || tier == "T4" || tier == "CA";
    }

    string SanitizeTierToken(string raw)
    {
        string result = "";
        string allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        raw.ToUpper();
        for (int i = 0; i < raw.Length(); i++)
        {
            string ch = raw.Get(i);
            if (allowed.Contains(ch))
                result += ch;
        }
        return result;
    }

    string TrimString(string s)
    {
        int start = 0;
        int end = s.Length();
        while (start < end && IsWhitespaceChar(s.Get(start))) start++;
        while (end > start && IsWhitespaceChar(s.Get(end - 1))) end--;
        if (start >= end) return "";
        return s.Substring(start, end - start);
    }

    bool IsWhitespaceChar(string ch)
    {
        return ch == " " || ch == "\t" || ch == "\r" || ch == "\n";
    }

    override void OnStart(float startingProgress = 0)
    {
        m_StartSucceeded = false;
        if (m_KOTHLocations.Count() == 0)
        {
            TASEventsManager.GetInstance().LogAction("ERROR: No KOTH locations loaded - start rejected.");
            return;
        }

        bool isResuming = m_IsResuming;
        TASEventsManager kothMgr = TASEventsManager.GetInstance();
        string savedLocation = kothMgr.GetActiveEventLocation("KOTH");
        vector savedPos;
        bool hasSavedPos = kothMgr.GetActiveEventPosition("KOTH", savedPos);
        bool hasUsableSavedPos = hasSavedPos && vector.Distance(savedPos, vector.Zero) > 0.01;
        int idx = -1;
        if (!isResuming && savedLocation != "")
        {
            kothMgr.LogAction("CLEARING STALE LOCATION: '" + savedLocation + "' was saved but no event is currently active - the previous event did not shut down cleanly.");
            kothMgr.ClearActiveEventLocation("KOTH");
            savedLocation = "";
        }
        if (isResuming)
        {
            if (savedLocation == "")
            {
                kothMgr.LogAction("RESUME REJECTED: saved active KOTH state has no location name.");
                return;
            }

            for (int i = 0; i < m_KOTHLocations.Count(); i++)
            {
                TASEventKOTHLocation location = m_KOTHLocations.Get(i);
                if (location && location.name == savedLocation && hasUsableSavedPos && vector.Distance(location.pos, savedPos) <= RESUME_POS_MATCH_RAD)
                {
                    idx = i;
                    break;
                }
            }

            // Older state files wrote a zero vector as a non-empty string.
            // Treat that as missing position, but only resume by name when the
            // name identifies exactly one configured location.
            if (idx < 0 && !hasUsableSavedPos)
            {
                int nameMatches = 0;
                int nameMatchIndex = -1;
                for (int legacy = 0; legacy < m_KOTHLocations.Count(); legacy++)
                {
                    TASEventKOTHLocation legacyLocation = m_KOTHLocations.Get(legacy);
                    if (legacyLocation && legacyLocation.name == savedLocation)
                    {
                        nameMatches++;
                        nameMatchIndex = legacy;
                    }
                }
                if (nameMatches == 1)
                {
                    idx = nameMatchIndex;
                    kothMgr.LogAction("WARNING: resuming legacy KOTH state by its unique location name; saved position was missing or zero.");
                }
                else if (nameMatches > 1)
                    kothMgr.LogAction("RESUME REJECTED: legacy location name '" + savedLocation + "' is ambiguous; clearing stale state instead of moving the event.");
            }

            if (idx < 0 && hasUsableSavedPos)
            {
                float bestDistance = float.MAX;
                for (int j = 0; j < m_KOTHLocations.Count(); j++)
                {
                    TASEventKOTHLocation candidate = m_KOTHLocations.Get(j);
                    if (!candidate || candidate.name != savedLocation) continue;
                    float candidateDistance = vector.Distance(candidate.pos, savedPos);
                    if (candidateDistance < bestDistance)
                    {
                        bestDistance = candidateDistance;
                        idx = j;
                    }
                }
                if (idx >= 0 && bestDistance > RESUME_POS_WARN_RAD)
                {
                    kothMgr.LogAction("WARNING: resumed location '" + savedLocation + "' moved " + bestDistance.ToString() + "m from the saved position - rejecting stale state.");
                    idx = -1;
                }
            }
            if (idx < 0)
            {
                string savedPosText = "(missing)";
                if (hasSavedPos) savedPosText = savedPos.ToString();
                kothMgr.LogAction("RESUME REJECTED: no safe location match for '" + savedLocation + "' at " + savedPosText + "; stale state will be cleared.");
                return;
            }
        }
        else if (idx < 0)
        {
            array<int> candidateOrder = new array<int>;
            for (int candidate = 0; candidate < m_KOTHLocations.Count(); candidate++)
            {
                if (candidate != m_LastLocationIndex)
                    candidateOrder.Insert(candidate);
            }
            if (m_LastLocationIndex >= 0 && m_LastLocationIndex < m_KOTHLocations.Count())
                candidateOrder.Insert(m_LastLocationIndex);

            while (candidateOrder.Count() > 0)
            {
                int pick = Math.RandomInt(0, candidateOrder.Count());
                int candidateIndex = candidateOrder.Get(pick);
                candidateOrder.Remove(pick);
                TASEventKOTHLocation candidateLocation = m_KOTHLocations.Get(candidateIndex);
                if (!candidateLocation) continue;

                if (IsPlayerNearby(candidateLocation.pos, 200))
                {
                    kothMgr.LogAction("LOCATION REJECTED | " + candidateLocation.name + " is within 200m of a player; trying another location.");
                    continue;
                }

                idx = candidateIndex;
                break;
            }

            if (idx < 0)
            {
                kothMgr.LogAction("SPAWN DEFERRED | all " + m_KOTHLocations.Count().ToString() + " KOTH locations have players within 200m; no cooldown charged.");
                return;
            }
        }

        m_ActiveLocation = m_KOTHLocations.Get(idx);
        if (!m_ActiveLocation)
        {
            TASEventsManager.GetInstance().LogAction("ERROR: Selected KOTH location is invalid - start rejected.");
            return;
        }

        if (isResuming)
            TASEventsManager.GetInstance().LogAction("PLAYER BLOCK BYPASSED: resuming the active event at " + m_ActiveLocation.name + " - the 200m player proximity check does not apply to a resume.");

        m_LastLocationIndex = idx;
        m_EventPos = m_ActiveLocation.pos;

        if (m_HasPendingCleanup)
            FinishPendingCleanup();

        m_TotalLifetime = m_EventElapsedSeconds;
        m_TimeoutGraceTimer = m_TimeoutGraceRemainingSeconds;
        m_Timer = 0;
        m_Points = (startingProgress / 100.0) * MAX_POINTS;
        m_Progress = startingProgress;
        m_LastDisplayedPct = -1;
        m_SmokeTriggered = false;
        m_ToxicZoneSpawned = false;
        m_InMissionArea.Clear();
        m_InCaptureArea.Clear();
        m_LastMissionZoneNotice.Clear();
        m_LastCaptureZoneNotice.Clear();
        m_ToxicZoneRetryPending = false;
        m_ToxicZoneRetryTimer = 0.0;
        m_ToxicZoneRetryCount = 0;
        m_ResumeFlagpolePending = isResuming;
        m_ResumeFlagpolePollTimer = 0;
        m_ResumeFlagpoleWaitTimer = 0;

        if (SpawnFlagpole(!isResuming))
            m_ResumeFlagpolePending = false;

        #ifdef LBmaster_Groups
        m_ActiveMarker = LBStaticMarkerManager.Get.AddTempServerMarker("KOTH" + " - 0%", m_ActiveLocation.pos, "LBmaster_Groups\\gui\\icons\\sniper.paa", 0xFF0000FF, true, true, true, true);
        if (m_ActiveMarker)
        {
            m_ActiveMarker.SetRadius(100.0, 0xFF0000FF, false);
            m_MarkerUID = m_ActiveMarker.uid;
        }
        #endif

        TASEventsManager mgr = TASEventsManager.GetInstance();
        mgr.SetActiveEventLocation(m_ActiveLocation.name, m_ActiveLocation.pos);
        mgr.LogAction("EVENT SPAWNED | location: " + m_ActiveLocation.name + " | tier: " + m_ActiveLocation.tier + " | position: " + m_ActiveLocation.pos.ToString() + " | time: " + mgr.GetTimestamp() + " | starting progress: " + startingProgress.ToString() + "%");

        if (!isResuming)
            SendGlobalInfo("A new KOTH event has appeared at " + m_ActiveLocation.name + "!");

        super.OnStart(startingProgress);
    }

    override void OnForceEnd()
    {
        TASEventsManager.GetInstance().LogAction("EVENT WATCHDOG CLEANUP | KOTH elapsed timer stopped advancing; ending event and removing its event visuals.");
        m_WasCaptured = false;
        OnEventEnded();
    }

    override void OnUpdate(float deltaT)
    {
        if (!m_IsRunning) return;
        if (!GetGame() || !m_ActiveLocation)
        {
            OnEventEnded();
            return;
        }

        if (m_ResumeFlagpolePending)
        {
            m_ResumeFlagpolePollTimer += deltaT;
            if (m_ResumeFlagpolePollTimer >= 5.0)
            {
                m_ResumeFlagpolePollTimer = 0;
                if (SpawnFlagpole(false))
                {
                    m_ResumeFlagpolePending = false;
                    TASEventsManager.GetInstance().LogAction("RESUMED EXISTING KOTH FLAGPOLE | location: " + m_ActiveLocation.name);
                }
            }
            if (m_ResumeFlagpolePending && IsPlayerNearby(m_ActiveLocation.pos, 300.0))
            {
                m_ResumeFlagpoleWaitTimer += deltaT;
                if (m_ResumeFlagpoleWaitTimer >= 15.0)
                {
                    m_ResumeFlagpoleWaitTimer = 0;
                    if (SpawnFlagpole(true))
                    {
                        m_ResumeFlagpolePending = false;
                        TASEventsManager.GetInstance().LogAction("CREATED KOTH FLAGPOLE AFTER RESUME | no saved pole found near " + m_ActiveLocation.name);
                    }
                }
            }
            else if (m_ResumeFlagpolePending)
                m_ResumeFlagpoleWaitTimer = 0;
        }

        m_Timer += deltaT;
        if (m_Timer >= 0.5)
        {
            HandleZones();
            HandlePoints();
            if (!m_IsRunning) return;
            HandleLifetime();
            if (!m_IsRunning) return;
            UpdateMarker();

            if (!m_SmokeTriggered && IsPlayerNearby(m_ActiveLocation.pos, 1500))
            {
                m_SmokeTriggered = true;
                ReplaceSmoke(false);
                TASEventsManager.GetInstance().LogAction("SMOKE ACTIVATED | location: " + m_ActiveLocation.name + " | time: " + TASEventsManager.GetInstance().GetTimestamp());
            }
            m_Timer = 0;
        }
    }

    override void OnCleanupUpdate(float deltaT)
    {
        if (m_ToxicZoneRetryPending)
        {
            m_ToxicZoneRetryTimer -= deltaT;
            if (m_ToxicZoneRetryTimer <= 0.0)
            {
                m_ToxicZoneRetryCount++;
                if (SpawnTimeoutToxicZone())
                {
                    m_ToxicZoneRetryPending = false;
                    TASEventsManager.GetInstance().LogAction("TOXIC ZONE RETRY SUCCEEDED | attempt: " + m_ToxicZoneRetryCount.ToString());
                }
                else if (m_ToxicZoneRetryCount >= TOXIC_ZONE_MAX_RETRIES)
                {
                    m_ToxicZoneRetryPending = false;
                    TASEventsManager.GetInstance().LogAction("TOXIC ZONE RETRIES EXHAUSTED | attempts: " + m_ToxicZoneRetryCount.ToString());
                }
                else
                    m_ToxicZoneRetryTimer = TOXIC_ZONE_RETRY_INTERVAL_SECONDS;
            }
        }

        if (!m_HasPendingCleanup) return;
        m_PendingCleanupTimer -= deltaT;
        if (m_PendingCleanupTimer <= 0)
            FinishPendingCleanup();
    }

    void FinishPendingCleanup()
    {
        m_HasPendingCleanup = false;
        if (m_ToxicZoneRetryPending)
        {
            m_ToxicZoneRetryPending = false;
            TASEventsManager.GetInstance().LogAction("TOXIC ZONE RETRY CANCELLED | visual cleanup deadline reached");
        }

        #ifdef LBmaster_Groups
        if (m_PendingMarkerUID != -1)
        {
            LBStaticMarkerManager.Get.RemoveServerMarker(m_PendingMarkerUID);
            m_PendingMarkerUID = -1;
        }
        #endif

        if (m_PendingFlagpole)
        {
            if (GetGame()) GetGame().ObjectDelete(m_PendingFlagpole);
            m_PendingFlagpole = null;
        }

        StopSmoke();

        TASEventsManager.GetInstance().LogAction("VISUAL CLEANUP DONE | flagpole deleted | marker removed | smoke stopped");
    }

    void UpdateMarker()
    {
        int threshold = (Math.Round(m_Progress) / 5) * 5;
        UpdateFlagHeight();
        if (threshold != m_LastDisplayedPct)
        {
            m_LastDisplayedPct = threshold;
            #ifdef LBmaster_Groups
            if (m_ActiveMarker)
                m_ActiveMarker.SetName("KOTH" + " - " + threshold + "%");
            #endif
        }
    }

    private bool CanSendMissionZoneNotice(PlayerBase player)
    {
        if (!player) return false;
        if (m_LastMissionZoneNotice.Contains(player) && m_TotalLifetime - m_LastMissionZoneNotice.Get(player) < ZONE_NOTICE_COOLDOWN_SECONDS)
            return false;
        m_LastMissionZoneNotice.Set(player, m_TotalLifetime);
        return true;
    }

    private bool CanSendCaptureZoneNotice(PlayerBase player)
    {
        if (!player) return false;
        if (m_LastCaptureZoneNotice.Contains(player) && m_TotalLifetime - m_LastCaptureZoneNotice.Get(player) < ZONE_NOTICE_COOLDOWN_SECONDS)
            return false;
        m_LastCaptureZoneNotice.Set(player, m_TotalLifetime);
        return true;
    }

    void HandleZones()
    {
        if (!GetGame() || !m_ActiveLocation) return;
        array<Man> players = new array<Man>;
        GetGame().GetPlayers(players);
        TASEventsManager mgr = TASEventsManager.GetInstance();
        ref map<PlayerBase, bool> missionNow = new map<PlayerBase, bool>;
        ref map<PlayerBase, bool> captureNow = new map<PlayerBase, bool>;

        foreach (Man man : players)
        {
            PlayerBase pb = PlayerBase.Cast(man);
            if (!pb || !pb.IsAlive()) continue;
            PlayerIdentity identity = pb.GetIdentity();
            if (!identity) continue;

            float dist = vector.Distance(pb.GetPosition(), m_ActiveLocation.pos);
            string pName = identity.GetName();
            bool wasInMission = m_InMissionArea && m_InMissionArea.Contains(pb);
            bool wasInCapture = m_InCaptureArea && m_InCaptureArea.Contains(pb);

            if (dist <= MISSION_RAD)
            {
                missionNow.Insert(pb, true);
                if (!wasInMission && CanSendMissionZoneNotice(pb))
                {
                    SendClientNotify(pb, "ENTERING KOTH MISSION AREA");
                    mgr.LogAction("ZONE ENTER | player: " + pName + " | zone: mission (100m) | location: " + m_ActiveLocation.name + " | time: " + mgr.GetTimestamp());
                }
            }
            else if (wasInMission && CanSendMissionZoneNotice(pb))
            {
                SendClientNotify(pb, "LEAVING KOTH MISSION AREA");
                mgr.LogAction("ZONE LEAVE | player: " + pName + " | zone: mission (100m) | location: " + m_ActiveLocation.name + " | time: " + mgr.GetTimestamp());
            }

            if (dist <= CAPTURE_RAD)
            {
                captureNow.Insert(pb, true);
                if (!wasInCapture)
                {
                    if (!m_HasEverStarted)
                    {
                        SendGlobalInfo("KOTH at " + m_ActiveLocation.name + " is being captured!");
                        m_HasEverStarted = true;
                    }
                    if (CanSendCaptureZoneNotice(pb))
                    {
                        SendClientNotify(pb, "YOU ARE CAPTURING THE HILL!");
                        mgr.LogAction("CAPTURE START | player: " + pName + " | location: " + m_ActiveLocation.name + " | progress: " + m_Progress.ToString() + "% | time: " + mgr.GetTimestamp());
                    }
                }
            }
            else if (wasInCapture && CanSendCaptureZoneNotice(pb))
            {
                SendClientNotify(pb, "LEAVING KOTH CAPTURE AREA!");
                mgr.LogAction("CAPTURE STOP | player: " + pName + " | location: " + m_ActiveLocation.name + " | progress: " + m_Progress.ToString() + "% | time: " + mgr.GetTimestamp());
            }
        }

        m_InMissionArea = missionNow;
        m_InCaptureArea = captureNow;
    }

    void HandlePoints()
    {
        if (m_InCaptureArea.Count() > 0)
            m_Points += 0.5;
        else
            m_Points = Math.Max(0, m_Points - 0.25);

        m_Progress = (m_Points / MAX_POINTS) * 100.0;
        UpdateFlagHeight();

        if (m_Points >= MAX_POINTS)
        {
            TASEventsManager mgr = TASEventsManager.GetInstance();
            mgr.LogAction("EVENT CAPTURED | location: " + m_ActiveLocation.name + " | tier: " + m_ActiveLocation.tier + " | time: " + mgr.GetTimestamp() + " | captors in zone: " + m_InCaptureArea.Count().ToString());
            SendGlobalInfo("KOTH at " + m_ActiveLocation.name + " has been captured!");
            m_WasCaptured = true;
            ReplaceSmoke(true);

            vector chestPos = m_ActiveLocation.pos + Vector(LOOTCHEST_OFFSET, 0, 0);
            chestPos[1] = GetGame().SurfaceY(chestPos[0], chestPos[2]);
            TASEventLootchest.GetInstance().SpawnLootchest(m_EventName, m_ActiveLocation.tier, chestPos, Vector(FLAG_HEADING, 0, 0));

            OnEventEnded();
        }
    }

    void HandleLifetime()
    {
        m_TotalLifetime += 0.5;
        m_EventElapsedSeconds = m_TotalLifetime;
        if (m_TotalLifetime < MAX_TIME) return;

        if (m_TimeoutGraceTimer > 0.0)
        {
            m_TimeoutGraceTimer -= 0.5;
            if (m_TimeoutGraceTimer > 0.0)
            {
                m_TimeoutGraceRemainingSeconds = m_TimeoutGraceTimer;
                return;
            }
            m_TimeoutGraceTimer = 0.0;
            m_TimeoutGraceRemainingSeconds = 0.0;
        }

        // The capture map was refreshed by HandleZones immediately before this
        // check. Keep the timed-out event alive for one more minute while at
        // least one valid player remains inside the 25m capture radius.
        if (m_InCaptureArea && m_InCaptureArea.Count() > 0)
        {
            m_TimeoutGraceTimer = TIMEOUT_CAPTURE_GRACE_SECONDS;
            m_TimeoutGraceRemainingSeconds = TIMEOUT_CAPTURE_GRACE_SECONDS;
            TASEventsManager mgr = TASEventsManager.GetInstance();
            mgr.LogAction("EVENT OVERTIME | capture zone occupied at the 30-minute limit; checking again in " + TIMEOUT_CAPTURE_GRACE_SECONDS.ToString() + " seconds");
            mgr.SaveState(m_EventName);
            return;
        }

        TASEventsManager mgr = TASEventsManager.GetInstance();
        mgr.LogAction("EVENT TIMEOUT | location: " + m_ActiveLocation.name + " | tier: " + m_ActiveLocation.tier + " | time: " + mgr.GetTimestamp() + " | final progress: " + m_Progress.ToString() + "% | capture zone empty after timeout/grace check");
        SendGlobalInfo("KOTH at " + m_ActiveLocation.name + " has been destroyed.");
        m_WasCaptured = false;

        if (!SpawnTimeoutToxicZone())
        {
            m_ToxicZoneRetryPending = true;
            m_ToxicZoneRetryTimer = TOXIC_ZONE_RETRY_INTERVAL_SECONDS;
            m_ToxicZoneRetryCount = 0;
        }

        OnEventEnded();
    }

    bool SpawnTimeoutToxicZone()
    {
        if (m_ToxicZoneSpawned) return true;
        if (!GetGame() || !m_ActiveLocation) return false;

        vector zonePos = m_ActiveLocation.pos;
        zonePos[1] = GetGame().SurfaceY(zonePos[0], zonePos[2]);

        Object obj = GetGame().CreateObjectEx("ContaminatedArea_Dynamic", zonePos, ECE_PLACE_ON_SURFACE);
        ContaminatedArea_Dynamic zone = ContaminatedArea_Dynamic.Cast(obj);
        if (!zone)
        {
            if (obj) GetGame().ObjectDelete(obj);
            TASEventsManager.GetInstance().LogAction("TOXIC ZONE SPAWN FAILED | location: " + m_ActiveLocation.name);
            return false;
        }

        zone.m_Position        = zonePos;
        zone.m_Radius          = TOXIC_RADIUS;
        zone.m_PositiveHeight  = TOXIC_POS_HEIGHT;
        zone.m_NegativeHeight  = TOXIC_NEG_HEIGHT;

        zone.m_TriggerType     = "ContaminatedTrigger";

        zone.m_ParticleID        = ParticleList.CONTAMINATED_AREA_GAS_BIGASS;
        zone.m_AroundParticleID  = ParticleList.CONTAMINATED_AREA_GAS_AROUND;
        zone.m_TinyParticleID    = ParticleList.CONTAMINATED_AREA_GAS_TINY;

        zone.m_PPERequesterType = "PPERequester_ContaminatedAreaTint";

        zone.m_InnerRings       = TOXIC_INNER_RINGS;
        zone.m_InnerSpacing     = TOXIC_INNER_SPACING;
        zone.m_OuterRingToggle  = TOXIC_OUTER_TOGGLE;
        zone.m_OuterSpacing     = TOXIC_OUTER_SPACING;
        zone.m_OuterRingOffset  = TOXIC_OUTER_OFFSET;
        zone.m_VerticalLayers   = TOXIC_V_LAYERS;
        zone.m_VerticalOffset   = TOXIC_V_OFFSET;

        zone.InitZone();

        m_ToxicZoneSpawned = true;

        string logMsg;
        logMsg  = "TOXIC ZONE SPAWNED (dynamic, self-cleaning) | location: " + m_ActiveLocation.name;
        logMsg += " | radius: " + TOXIC_RADIUS.ToString();
        logMsg += " | position: " + zonePos.ToString();
        logMsg += " | lifetime: ~30s warning + 900s active + 300s decay (vanilla defaults)";
        TASEventsManager.GetInstance().LogAction(logMsg);
        return true;
    }

    void OnEventEnded()
    {
        m_IsRunning = false;
        m_IsCleaningUp = false;

        m_InMissionArea.Clear();
        m_InCaptureArea.Clear();
        m_LastMissionZoneNotice.Clear();
        m_LastCaptureZoneNotice.Clear();

        if (!m_WasCaptured)
            RemoveMarker();

        if (m_HasPendingCleanup)
            FinishPendingCleanup();

        m_PendingFlagpole = m_EventFlagpole;
        m_PendingMarkerUID = m_MarkerUID;
        m_PendingCleanupTimer = CLEANUP_DELAY_SECONDS;
        m_HasPendingCleanup = true;

        m_EventFlagpole = null;
        m_MarkerUID = -1;
        m_ActiveMarker = null;

        TASEventsManager.GetInstance().MarkEventEnded(m_EventName);

        super.OnEnd();
    }

    void RemoveMarker()
    {
        #ifdef LBmaster_Groups
        if (m_MarkerUID != -1)
        {
            LBStaticMarkerManager.Get.RemoveServerMarker(m_MarkerUID);
            m_MarkerUID = -1;
            m_ActiveMarker = null;
        }
        #endif
    }

    bool SpawnFlagpole(bool allowCreate = true)
    {
        if (!GetGame() || !m_ActiveLocation) return false;

        m_EventFlagpole = FindExistingFlagpole(m_ActiveLocation.pos);
        if (!m_EventFlagpole && allowCreate)
        {
            Object poleObject = GetGame().CreateObjectEx("TAS_KOTHFlagPole", m_ActiveLocation.pos, ECE_SETUP | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH);
            m_EventFlagpole = TerritoryFlag.Cast(poleObject);
            if (!m_EventFlagpole) return false;

            m_EventFlagpole.SetPosition(m_ActiveLocation.pos);
            m_EventFlagpole.SetOrientation(Vector(FLAG_HEADING, 0, 0));
            m_EventFlagpole.FullyBuild();
            RemoveNearbyFlagpoleKits(m_EventFlagpole.GetPosition());
        }
        if (!m_EventFlagpole) return false;

        m_EventFlagpole.SetAllowDamage(false);
        GameInventory inventory = m_EventFlagpole.GetInventory();
        EntityAI flag;
        if (inventory)
        {
            flag = m_EventFlagpole.FindAttachmentBySlotName("Material_FPole_Flag");
            if (flag && flag.GetType() != "Flag_Cannibals")
            {
                GetGame().ObjectDelete(flag);
                flag = null;
            }
            if (!flag)
                flag = inventory.CreateAttachment("Flag_Cannibals");
        }
        if (flag)
        {
            flag.SetAllowDamage(false);
            inventory.LockInventory(LOCK_FROM_SCRIPT);
            m_EventFlagpole.AnimateFlagEx(1.0);
        }

        m_LastFlagPercent = -1;
        UpdateFlagHeight();
        return true;
    }

    TerritoryFlag FindExistingFlagpole(vector position)
    {
        array<Object> nearbyObjects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition3D(position, 8.0, nearbyObjects, proxyCargos);

        TerritoryFlag existingPole;
        foreach (Object nearbyObject : nearbyObjects)
        {
            if (!nearbyObject || nearbyObject.GetType() != "TAS_KOTHFlagPole") continue;
            TerritoryFlag nearbyPole = TerritoryFlag.Cast(nearbyObject);
            if (!nearbyPole) continue;
            if (!existingPole)
                existingPole = nearbyPole;
            else
                GetGame().ObjectDelete(nearbyPole);
        }

        if (existingPole)
            RemoveNearbyFlagpoleKits(existingPole.GetPosition());
        return existingPole;
    }

    void RemoveNearbyFlagpoleKits(vector position)
    {
        array<Object> nearbyObjects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition3D(position, 8.0, nearbyObjects, proxyCargos);
        foreach (Object nearbyObject : nearbyObjects)
        {
            if (!nearbyObject || nearbyObject.GetType() != "TerritoryFlagKit") continue;
            if (vector.Distance(nearbyObject.GetPosition(), position) <= 5.0)
                GetGame().ObjectDelete(nearbyObject);
        }
    }

    void UpdateFlagHeight()
    {
        if (!m_EventFlagpole) return;

        int flagPercent = Math.Round(m_Progress);
        if (flagPercent < 0) flagPercent = 0;
        if (flagPercent > 100) flagPercent = 100;
        if (flagPercent == m_LastFlagPercent) return;

        m_LastFlagPercent = flagPercent;
        m_EventFlagpole.AnimateFlagEx(1.0 - (flagPercent / 100.0));
        m_EventFlagpole.SetAllowDamage(false);
    }

    void ReplaceSmoke(bool green)
    {
        StopSmoke();
        if (!GetGame() || !m_ActiveLocation) return;

        m_SmokeTriggered = true;
        string grenadeType;
        if (green)
            grenadeType = "M18SmokeGrenade_Green";
        else
            grenadeType = "M18SmokeGrenade_White";

        vector smokePos = m_ActiveLocation.pos;
        smokePos[1] = GetGame().SurfaceY(smokePos[0], smokePos[2]);
        Object smokeObject = GetGame().CreateObjectEx(grenadeType, smokePos, ECE_SETUP | ECE_CREATEPHYSICS);
        SmokeGrenadeBase smoke = SmokeGrenadeBase.Cast(smokeObject);
        if (!smoke) return;

        smoke.SetPosition(smokePos);
        smoke.SetAllowDamage(false);
        smoke.SetLifetime(7200.0);
        m_SmokeGrenades.Insert(smoke);
        smoke.OnWorkStart();
    }

    void StopSmoke()
    {
        if (!m_SmokeGrenades) return;

        foreach (SmokeGrenadeBase smoke : m_SmokeGrenades)
        {
            if (!smoke) continue;
            smoke.OnWorkStop();
            smoke.SetLifetime(8.0);
        }
        m_SmokeGrenades.Clear();
    }

    void SendGlobalInfo(string msg)
    {
        if (!GetGame()) return;
        array<Man> all = new array<Man>;
        GetGame().GetPlayers(all);
        foreach (Man m : all)
        {
            PlayerBase pb = PlayerBase.Cast(m);
            if (!pb) continue;
            PlayerIdentity identity = pb.GetIdentity();
            if (!identity) continue;

            GetRPCManager().SendRPC("TASEvents", "ShowNotify", new Param1<string>(msg), true, identity);
            #ifdef LBmaster_Groups
            LBNotificationSystem.LBSendNotificationTranslated(identity, 5, "KING OF THE HILL", new LBTranslatedString(msg), LBIconConfig.Get.info);
            #endif
        }
    }

    void SendClientNotify(PlayerBase p, string msg)
    {
        if (!p) return;
        PlayerIdentity identity = p.GetIdentity();
        if (identity)
            GetRPCManager().SendRPC("TASEvents", "ShowNotify", new Param1<string>(msg), true, identity);
    }

    bool IsPlayerNearby(vector pos, float rad)
    {
        if (!GetGame()) return false;
        array<Man> p = new array<Man>;
        GetGame().GetPlayers(p);
        foreach (Man m : p)
        {
            PlayerBase pb = PlayerBase.Cast(m);
            if (pb && pb.IsAlive() && vector.Distance(pb.GetPosition(), pos) < rad) return true;
        }
        return false;
    }
}