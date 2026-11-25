class TASEventTypeConfig
{
    bool Enabled = true;
    int MinPlayers = 2;
    int MinTimeBetweenEventsMinutes = 30;
}

class TASEventState
{
    bool IsEventActive = false;
    string ActiveEventName = "";
    float ActiveEventProgress = 0.0;
    float ActiveEventElapsedSeconds = 0.0;
    float TimeoutGraceRemainingSeconds = 0.0;
    string ActiveEventLocation = "";
    string ActiveEventPosition = "";
    string EventSpawnTimestamp = "";
    string LastEventEndTimestamp = "";
    int LastEventEndMinutes = 0;
    int MarkerUID = -1;
    bool HasStarted = false;
}

class TASEventBase
{
    string m_EventName;
    float m_Progress;
    float m_EventElapsedSeconds;
    float m_TimeoutGraceRemainingSeconds = 0.0;
    bool m_IsRunning;
    int m_MarkerUID = -1;
    bool m_HasEverStarted = false;
    bool m_IsCleaningUp = false;
    bool m_HasPendingCleanup = false;
    bool m_IsResuming = false;
    bool m_StartSucceeded = false;
    vector m_EventPos;

    void OnStart(float startingProgress = 0)
    {
        m_IsRunning = true;
        m_Progress = startingProgress;
        m_StartSucceeded = true;
    }

    void OnUpdate(float deltaT) {}

    void OnCleanupUpdate(float deltaT) {}

    void OnForceEnd()
    {
        OnEnd();
    }

    void OnEnd()
    {
        m_IsRunning = false;
        m_IsCleaningUp = false;
        if (TASEventsManager.GetInstance())
            TASEventsManager.GetInstance().OnEventFinished(m_EventName);
    }

    string GetEventName() { return m_EventName; }
}

class TASEventsManager
{
    private static ref TASEventsManager m_Instance;
    private ref map<string, ref TASEventBase> m_RegisteredEvents;
    private ref map<string, ref TASEventTypeConfig> m_EventConfigs;
    private ref map<string, ref TASEventState> m_EventStates;
    private ref array<string> m_EventTypes;
    private ref array<ref TASEventBase> m_CleanupQueue;
    private TASEventBase m_CurrentActiveEvent;
    private string m_CurrentActiveEventType = "";
    private float m_CheckTimer = 0;
    private float m_SaveTimer = 0;
    private bool m_ResumeChecked = false;
    private float m_EventStallTimer = 0.0;
    private float m_LastObservedEventElapsedSeconds = 0.0;

    private const float CHECK_INTERVAL = 60.0;
    private const float SAVE_INTERVAL = 30.0;
    private const float EVENT_STALL_LIMIT_SECONDS = 120.0;

    void TASEventsManager()
    {
        m_RegisteredEvents = new map<string, ref TASEventBase>;
        m_EventConfigs = new map<string, ref TASEventTypeConfig>;
        m_EventStates = new map<string, ref TASEventState>;
        m_EventTypes = new array<string>;
        m_CleanupQueue = new array<ref TASEventBase>;
        LoadFiles();
    }

    static TASEventsManager GetInstance()
    {
        if (!m_Instance) m_Instance = new TASEventsManager();
        return m_Instance;
    }

    void RegisterEvent(TASEventBase pEvent)
    {
        if (pEvent) m_RegisteredEvents.Insert(pEvent.GetEventName(), pEvent);
    }

    // Call after all events have registered. A saved active flag for an event
    // that this build cannot register must not silently block scheduling.
    void FinalizeEventRegistration()
    {
        foreach (string eventType : m_EventTypes)
        {
            if (!m_EventStates.Contains(eventType)) continue;
            TASEventState state = m_EventStates.Get(eventType);
            if (!state || !state.IsEventActive) continue;

            if (state.ActiveEventName != "" && state.ActiveEventName != eventType)
            {
                ResetStaleActiveState(eventType, "saved active event name does not match its configured type", true);
                continue;
            }

            if (!m_RegisteredEvents.Contains(eventType))
            {
                ResetStaleActiveState(eventType, "active state has no registered event implementation", true);
            }
        }

        LogAction("EVENT MANAGER READY | registered event types: " + m_RegisteredEvents.Count().ToString());
    }

    private void ResetStaleActiveState(string eventType, string reason, bool clearCooldown)
    {
        if (!m_EventStates || !m_EventStates.Contains(eventType)) return;
        TASEventState state = m_EventStates.Get(eventType);
        if (!state) return;

        state.IsEventActive = false;
        state.ActiveEventName = "";
        state.ActiveEventProgress = 0.0;
        state.ActiveEventElapsedSeconds = 0.0;
        state.TimeoutGraceRemainingSeconds = 0.0;
        state.ActiveEventLocation = "";
        state.ActiveEventPosition = "";
        state.EventSpawnTimestamp = "";
        state.MarkerUID = -1;
        state.HasStarted = false;
        if (clearCooldown)
        {
            state.LastEventEndTimestamp = "";
            state.LastEventEndMinutes = 0;
        }
        SaveState(eventType);
        LogAction("STALE EVENT STATE CLEARED | type: " + eventType + " | reason: " + reason);
    }

    private string Pad2(int val)
    {
        if (val < 10) return "0" + val.ToString();
        return val.ToString();
    }

    string GetTimestamp()
    {
        int yr, mt, d, h, m, s;
        GetYearMonthDay(yr, mt, d);
        GetHourMinuteSecond(h, m, s);
        string ts = yr.ToString() + "-" + Pad2(mt) + "-" + Pad2(d) + " " + Pad2(h) + ":" + Pad2(m) + ":" + Pad2(s);
        return ts;
    }

    private int GetTotalMinutes()
    {
        int yr, mt, d, h, m, s;
        GetYearMonthDay(yr, mt, d);
        GetHourMinuteSecond(h, m, s);
        return yr * 525960 + mt * 43830 + d * 1440 + h * 60 + m;
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

    void Update(float deltaT)
    {
        if (!GetGame()) return;
        if (TASEventLootchest.GetInstance())
            TASEventLootchest.GetInstance().Update(deltaT);
        if (!m_CleanupQueue || !m_RegisteredEvents || !m_EventStates || !m_EventConfigs || !m_EventTypes) return;

        for (int c = m_CleanupQueue.Count() - 1; c >= 0; c--)
        {
            TASEventBase cleanupEvent = m_CleanupQueue.Get(c);
            if (!cleanupEvent || !cleanupEvent.m_HasPendingCleanup)
            {
                m_CleanupQueue.Remove(c);
                continue;
            }
            cleanupEvent.OnCleanupUpdate(deltaT);
            if (!cleanupEvent.m_HasPendingCleanup)
                m_CleanupQueue.Remove(c);
        }

        if (m_CurrentActiveEvent && !m_CurrentActiveEvent.m_IsRunning && !m_CurrentActiveEvent.m_IsCleaningUp)
        {
            string orphanType = m_CurrentActiveEventType;
            m_CurrentActiveEvent = null;
            m_CurrentActiveEventType = "";
            ResetStaleActiveState(orphanType, "manager retained an event object that was neither running nor cleaning up", true);
        }

        bool eventRunning = (m_CurrentActiveEvent && m_CurrentActiveEvent.m_IsRunning);
        bool eventCleanup = (m_CurrentActiveEvent && m_CurrentActiveEvent.m_IsCleaningUp);
        if (eventRunning || eventCleanup)
        {
            TASEventBase updatingEvent = m_CurrentActiveEvent;
            updatingEvent.OnUpdate(deltaT);

            if (m_CurrentActiveEvent && m_CurrentActiveEvent.m_IsRunning)
            {
                if (m_CurrentActiveEvent.m_EventElapsedSeconds > m_LastObservedEventElapsedSeconds + 0.01)
                {
                    m_LastObservedEventElapsedSeconds = m_CurrentActiveEvent.m_EventElapsedSeconds;
                    m_EventStallTimer = 0.0;
                }
                else
                {
                    m_EventStallTimer += deltaT;
                    if (m_EventStallTimer >= EVENT_STALL_LIMIT_SECONDS)
                    {
                        LogAction("EVENT WATCHDOG | type: " + m_CurrentActiveEventType + " | elapsed time stopped advancing for " + m_EventStallTimer.ToString() + " seconds; forcing cleanup");
                        m_CurrentActiveEvent.OnForceEnd();
                    }
                }
            }

            m_SaveTimer += deltaT;
            if (m_SaveTimer >= SAVE_INTERVAL)
            {
                m_SaveTimer = 0;
                if (m_CurrentActiveEvent) SaveState(m_CurrentActiveEventType);
            }
            return;
        }

        if (!m_ResumeChecked)
        {
            m_ResumeChecked = true;
            foreach (string eventType : m_EventTypes)
            {
                if (!m_EventStates.Contains(eventType)) continue;
                TASEventState st = m_EventStates.Get(eventType);
                if (!st.IsEventActive) continue;
                if (!m_RegisteredEvents.Contains(eventType))
                {
                    ResetStaleActiveState(eventType, "active state survived registration but event type is unavailable", true);
                    continue;
                }

                ResumeEvent(eventType);
                if (m_CurrentActiveEvent && m_CurrentActiveEvent.m_IsRunning) return;
            }
        }

        m_CheckTimer += deltaT;
        if (m_CheckTimer >= CHECK_INTERVAL)
        {
            m_CheckTimer = 0;
            CheckSpawningLogic();
        }
    }

    private void CheckSpawningLogic()
    {
        if (m_CurrentActiveEvent) return;

        if (!GetGame()) return;
        array<Man> all = new array<Man>;
        GetGame().GetPlayers(all);
        int playerCount = 0;
        foreach (Man man : all)
        {
            PlayerBase player = PlayerBase.Cast(man);
            if (player && player.IsAlive() && player.GetIdentity())
                playerCount++;
        }
        LogAction("SPAWN CHECK | alive identified players: " + playerCount.ToString());
        if (playerCount == 0) return;

        foreach (string eventType : m_EventTypes)
        {
            if (!m_RegisteredEvents.Contains(eventType))
            {
                LogAction("SPAWN SKIPPED | type: " + eventType + " | no event implementation registered");
                continue;
            }
            if (!m_EventConfigs.Contains(eventType))
            {
                LogAction("SPAWN SKIPPED | type: " + eventType + " | no config loaded");
                continue;
            }

            TASEventTypeConfig cfg = m_EventConfigs.Get(eventType);
            if (!cfg)
            {
                LogAction("SPAWN SKIPPED | type: " + eventType + " | config is null");
                continue;
            }
            if (!cfg.Enabled)
            {
                LogAction("SPAWN SKIPPED | type: " + eventType + " | disabled in config");
                continue;
            }
            if (playerCount < cfg.MinPlayers)
            {
                LogAction("SPAWN SKIPPED | type: " + eventType + " | player count " + playerCount.ToString() + " below minimum " + cfg.MinPlayers.ToString());
                continue;
            }

            if (m_EventStates.Contains(eventType))
            {
                TASEventState st = m_EventStates.Get(eventType);
                if (!st) continue;
                int elapsed = GetTotalMinutes() - st.LastEventEndMinutes;
                if (elapsed < cfg.MinTimeBetweenEventsMinutes)
                {
                    LogAction("SPAWN SKIPPED | type: " + eventType + " | cooldown remaining: " + (cfg.MinTimeBetweenEventsMinutes - elapsed).ToString() + " minutes");
                    continue;
                }
            }

            StartEvent(eventType, 0);
            return;
        }
    }

    private void ResumeEvent(string eventType)
    {
        if (!m_EventStates.Contains(eventType)) return;
        if (!m_RegisteredEvents.Contains(eventType)) return;

        TASEventState st = m_EventStates.Get(eventType);
        if (!st) return;
        m_CurrentActiveEvent = m_RegisteredEvents.Get(eventType);
        if (!m_CurrentActiveEvent) return;
        m_CurrentActiveEventType = eventType;
        m_CurrentActiveEvent.m_MarkerUID = st.MarkerUID;
        m_CurrentActiveEvent.m_EventElapsedSeconds = st.ActiveEventElapsedSeconds;
        m_CurrentActiveEvent.m_TimeoutGraceRemainingSeconds = st.TimeoutGraceRemainingSeconds;
        m_CurrentActiveEvent.m_HasEverStarted = st.HasStarted;
        m_CurrentActiveEvent.m_IsResuming = true;
        m_CurrentActiveEvent.m_StartSucceeded = false;
        m_CurrentActiveEvent.OnStart(st.ActiveEventProgress);
        if (!m_CurrentActiveEvent || !m_CurrentActiveEvent.m_IsRunning || !m_CurrentActiveEvent.m_StartSucceeded)
        {
            if (m_CurrentActiveEvent)
            {
                m_CurrentActiveEvent.m_IsRunning = false;
                m_CurrentActiveEvent = null;
                m_CurrentActiveEventType = "";
            }
            ResetStaleActiveState(eventType, "resume attempt did not produce a running event", true);
            LogAction("RESUME ABORTED | type: " + eventType + " | stale state cleared; normal spawning may continue");
            return;
        }
        m_CurrentActiveEvent.m_IsResuming = false;
        m_EventStallTimer = 0.0;
        m_LastObservedEventElapsedSeconds = m_CurrentActiveEvent.m_EventElapsedSeconds;

        string msg = "EVENT RESUMED after server restart | type: " + eventType + " | progress: " + st.ActiveEventProgress.ToString() + "%";
        if (st.ActiveEventLocation != "")
            msg = msg + " | location: " + st.ActiveEventLocation + " | originally spawned: " + st.EventSpawnTimestamp;
        LogEventAction(eventType, msg);
    }

    void StartEvent(string eventType, float progress)
    {
        if (!m_RegisteredEvents || !m_RegisteredEvents.Contains(eventType)) return;

        m_CurrentActiveEvent = m_RegisteredEvents.Get(eventType);
        if (!m_CurrentActiveEvent) return;
        m_CurrentActiveEventType = eventType;

        TASEventState st;
        if (m_EventStates.Contains(eventType))
            st = m_EventStates.Get(eventType);
        else
        {
            st = new TASEventState();
            m_EventStates.Insert(eventType, st);
        }

        // FIX: mark the event active and clear any stale location *before*
        // OnStart() runs, so an abort or a restart mid-spawn can never leave a
        // location name paired with an empty position in the state file.
        st.IsEventActive = true;
        st.ActiveEventName = eventType;
        st.ActiveEventLocation = "";
        st.ActiveEventPosition = "";
        st.EventSpawnTimestamp = GetTimestamp();
        SaveState(eventType);

        st.IsEventActive = true;
        st.ActiveEventName = eventType;
        st.EventSpawnTimestamp = GetTimestamp();
        st.ActiveEventLocation = "";
        st.ActiveEventPosition = "";
        st.MarkerUID = -1;
        st.HasStarted = false;
        st.ActiveEventProgress = progress;
        st.TimeoutGraceRemainingSeconds = 0.0;
        st.LastEventEndTimestamp = "";
        st.LastEventEndMinutes = 0;

        m_CurrentActiveEvent.m_HasEverStarted = false;
        m_CurrentActiveEvent.m_EventElapsedSeconds = 0.0;
        m_CurrentActiveEvent.m_TimeoutGraceRemainingSeconds = 0.0;
        m_CurrentActiveEvent.m_IsResuming = false;
        m_CurrentActiveEvent.m_StartSucceeded = false;

        m_CurrentActiveEvent.OnStart(progress);
        if (!m_CurrentActiveEvent || !m_CurrentActiveEvent.m_IsRunning || !m_CurrentActiveEvent.m_StartSucceeded)
        {
            if (m_CurrentActiveEvent)
            {
                m_CurrentActiveEvent.m_IsRunning = false;
                m_CurrentActiveEvent = null;
                m_CurrentActiveEventType = "";
            }
            ResetStaleActiveState(eventType, "spawn attempt found no viable location or failed to initialize", true);
            LogAction("EVENT START ABORTED | type: " + eventType + " | no cooldown charged");
            return;
        }
        m_EventStallTimer = 0.0;
        m_LastObservedEventElapsedSeconds = m_CurrentActiveEvent.m_EventElapsedSeconds;

        string logMsg = "EVENT SPAWNED | type: " + eventType + " | time: " + st.EventSpawnTimestamp;
        if (st.ActiveEventLocation != "")
            logMsg = logMsg + " | location: " + st.ActiveEventLocation;
        LogEventAction(eventType, logMsg);

        SaveState(eventType);
    }

    void MarkEventEnded(string eventName)
    {
        TASEventState st;
        if (m_EventStates.Contains(eventName))
            st = m_EventStates.Get(eventName);
        else
        {
            st = new TASEventState();
            m_EventStates.Insert(eventName, st);
        }

        st.IsEventActive = false;
        st.LastEventEndTimestamp = GetTimestamp();
        st.LastEventEndMinutes = GetTotalMinutes();
        SaveState(eventName);

        int cooldown = 0;
        if (m_EventConfigs.Contains(eventName))
            cooldown = m_EventConfigs.Get(eventName).MinTimeBetweenEventsMinutes;

        LogEventAction(eventName, "EVENT END TIME RECORDED | type: " + eventName + " | time: " + st.LastEventEndTimestamp + " | cooldown before next event: " + cooldown.ToString() + " minutes");
    }

    void OnEventFinished(string eventName)
    {
        string ts = GetTimestamp();
        LogEventAction(eventName, "EVENT ENDED | type: " + eventName + " | time: " + ts);

        TASEventState st;
        if (m_EventStates.Contains(eventName))
            st = m_EventStates.Get(eventName);
        else
        {
            st = new TASEventState();
            m_EventStates.Insert(eventName, st);
        }

        st.IsEventActive = false;
        st.ActiveEventName = "";
        st.ActiveEventProgress = 0.0;
        st.ActiveEventElapsedSeconds = 0.0;
        st.TimeoutGraceRemainingSeconds = 0.0;
        st.ActiveEventLocation = "";
        st.ActiveEventPosition = "";
        st.EventSpawnTimestamp = "";

        if (st.LastEventEndTimestamp == "")
        {
            st.LastEventEndTimestamp = ts;
            st.LastEventEndMinutes = GetTotalMinutes();
        }

        st.MarkerUID = -1;
        st.HasStarted = false;

        if (m_RegisteredEvents.Contains(eventName))
        {
            TASEventBase endedEvent = m_RegisteredEvents.Get(eventName);
            if (endedEvent && endedEvent.m_HasPendingCleanup && m_CleanupQueue.Find(endedEvent) == -1)
                m_CleanupQueue.Insert(endedEvent);
        }

        m_CurrentActiveEvent = null;
        m_CurrentActiveEventType = "";
        m_SaveTimer = 0;
        m_EventStallTimer = 0.0;
        m_LastObservedEventElapsedSeconds = 0.0;
        SaveState(eventName);
    }

    string GetActiveEventLocation(string eventType)
    {
        if (!m_EventStates || !m_EventStates.Contains(eventType)) return "";
        TASEventState state = m_EventStates.Get(eventType);
        if (!state) return "";
        return state.ActiveEventLocation;
    }

    bool GetActiveEventPosition(string eventType, out vector position)
    {
        position = vector.Zero;
        if (!m_EventStates || !m_EventStates.Contains(eventType)) return false;
        TASEventState state = m_EventStates.Get(eventType);
        if (!state || state.ActiveEventPosition == "") return false;
        position = state.ActiveEventPosition.ToVector();
        return true;
    }

    void SetActiveEventLocation(string locationName, vector locationPos = "0 0 0")
    {
        if (m_CurrentActiveEventType == "") return;
        if (!m_EventStates.Contains(m_CurrentActiveEventType)) return;
        m_EventStates.Get(m_CurrentActiveEventType).ActiveEventLocation = locationName;
        m_EventStates.Get(m_CurrentActiveEventType).ActiveEventPosition = locationPos.ToString();
        SaveState(m_CurrentActiveEventType);
    }

    void ClearActiveEventLocation(string eventType)
    {
        if (!m_EventStates || !m_EventStates.Contains(eventType)) return;
        m_EventStates.Get(eventType).ActiveEventLocation = "";
        m_EventStates.Get(eventType).ActiveEventPosition = "";
        // FIX: persist the clear immediately. Without this the stale location
        // was written back by the next SaveState, e.g. on shutdown.
        SaveState(eventType);
    }

    private void LogServer(string text)
    {
        if (!FileExist("$profile:TASEvents")) MakeDirectory("$profile:TASEvents");
        FileHandle fh = OpenFile("$profile:TASEvents/TASEventsServer.log", FileMode.APPEND);
        if (fh)
        {
            FPrintln(fh, "[" + GetTimestamp() + "] " + text);
            CloseFile(fh);
        }
    }

    void LogEventAction(string eventType, string text)
    {
        string folder = "$profile:TASEvents/" + eventType;
        if (!FileExist(folder)) MakeDirectory(folder);
        FileHandle fh = OpenFile(folder + "/" + eventType + "eventlog.log", FileMode.APPEND);
        if (fh)
        {
            FPrintln(fh, "[" + GetTimestamp() + "] " + text);
            CloseFile(fh);
        }
    }

    void LogAction(string text)
    {
        if (m_CurrentActiveEventType != "")
        {
            LogEventAction(m_CurrentActiveEventType, text);
            return;
        }
        LogServer(text);
    }

    void OnKillInZone(PlayerBase victim, EntityAI killer)
    {
        if (!victim || !victim.GetIdentity()) return;
        if (!m_CurrentActiveEvent || !m_CurrentActiveEvent.m_IsRunning) return;
        if (!m_CurrentActiveEvent.m_IsRunning) return;
        if (vector.Distance(victim.GetPosition(), m_CurrentActiveEvent.m_EventPos) > 100.0) return;

        string vName = victim.GetIdentity().GetName();
        string kName = "Environmental";
        string weapon = "Unknown";
        if (killer)
        {
            PlayerBase kPlayer = PlayerBase.Cast(killer.GetHierarchyRootPlayer());
            if (kPlayer && kPlayer.GetIdentity()) kName = kPlayer.GetIdentity().GetName();
            weapon = killer.GetDisplayName();
        }

        string logLine = "KILL | victim: " + vName + " | killer: " + kName + " | weapon: [" + weapon + "] | time: " + GetTimestamp();
        LogEventAction(m_CurrentActiveEventType, logLine);
    }

    private void LoadFiles()
    {
        if (!FileExist("$profile:TASEvents")) MakeDirectory("$profile:TASEvents");

        string typesPath = "$profile:TASEvents/eventTypes.txt";
        FileHandle fh;
        if (FileExist(typesPath))
        {
            fh = OpenFile(typesPath, FileMode.READ);
            if (fh)
            {
                string line;
                while (FGets(fh, line) > 0)
                {
                    line = TrimString(line);
                    if (line != "") m_EventTypes.Insert(line);
                }
                CloseFile(fh);
            }
        }
        else
        {
            fh = OpenFile(typesPath, FileMode.WRITE);
            if (fh) { FPrintln(fh, "KOTH"); CloseFile(fh); }
            m_EventTypes.Insert("KOTH");
            LogAction("Created default eventTypes.txt with KOTH");
        }

        foreach (string eventType : m_EventTypes)
        {
            string folder = "$profile:TASEvents/" + eventType;
            if (!FileExist(folder)) MakeDirectory(folder);

            TASEventTypeConfig cfg = new TASEventTypeConfig();
            string cfgPath = folder + "/" + eventType + "Config.json";
            if (FileExist(cfgPath))
                JsonFileLoader<TASEventTypeConfig>.JsonLoadFile(cfgPath, cfg);
            else
            {
                JsonFileLoader<TASEventTypeConfig>.JsonSaveFile(cfgPath, cfg);
                LogAction("Created default config for event type '" + eventType + "' at: " + cfgPath);
            }
            m_EventConfigs.Insert(eventType, cfg);

            TASEventState st = new TASEventState();
            string statePath = folder + "/" + eventType + "lastEvent.json";
            if (FileExist(statePath))
                JsonFileLoader<TASEventState>.JsonLoadFile(statePath, st);
            else
                JsonFileLoader<TASEventState>.JsonSaveFile(statePath, st);
            m_EventStates.Insert(eventType, st);
        }

        TASEventLootchest.GetInstance().EnsureConfigs(m_EventTypes);
    }

    void SaveState(string eventType)
    {
        if (!m_EventStates.Contains(eventType)) return;

        TASEventState st = m_EventStates.Get(eventType);
        if (m_CurrentActiveEvent && m_CurrentActiveEventType == eventType)
        {
            st.ActiveEventProgress = m_CurrentActiveEvent.m_Progress;
            st.ActiveEventElapsedSeconds = m_CurrentActiveEvent.m_EventElapsedSeconds;
            st.TimeoutGraceRemainingSeconds = m_CurrentActiveEvent.m_TimeoutGraceRemainingSeconds;
            st.MarkerUID = m_CurrentActiveEvent.m_MarkerUID;
            st.HasStarted = m_CurrentActiveEvent.m_HasEverStarted;
        }

        string statePath = "$profile:TASEvents/" + eventType + "/" + eventType + "lastEvent.json";
        JsonFileLoader<TASEventState>.JsonSaveFile(statePath, st);
    }
}