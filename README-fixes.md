# TASEventManager / TASEventKOTH source fixes

The repository originally contained only `TASEventManager_and_TASEventKOTH_unpacked.zip`, which holds the two unpacked-PBO archives and their profile snapshot. The edited Enforce Script sources are also kept in `TASEventManager_source/` and `TASEventKOTH_source/` for review. The nested archives inside the original bundle are synchronized with those sources.

## Changes

- Clear and log orphaned active-event state after event registration when the event type is unavailable or the saved event cannot be resumed.
- Make event start success explicit, preventing failed starts from being logged as successful or charging a cooldown.
- Retry shuffled KOTH locations when a candidate has players within 200 m; defer without cooldown if all locations are occupied.
- Treat legacy zero-vector resume positions as missing; name-only recovery is allowed only when that name is unique.
- Add a 120-second elapsed-time watchdog with KOTH cleanup, plus periodic spawn-gate diagnostics.
- Remove strong `ref` qualifiers from the client RPC callback arguments flagged by the script warning.
- Include named loot-pool entries in the configured minimum-loot fallback.
- Throttle each player's mission/capture boundary notices to one per zone every 10 seconds and suppress the misleading global “new KOTH” message on resume.
- Retry a failed timeout contaminated-area creation up to five times at 15-second intervals while event visuals are cleaning up.
- At the 30-minute limit, keep the event in one-minute intervals while the capture area is occupied; timeout only when a check finds it empty. Persist the remaining grace timer across restart.

## GasRaidSiren compatibility

Inspected `GasRaidSiren.zip` from the latest `origin/main` tree. Its `modded class ContaminatedArea_Dynamic` only overrides `PlayFlareVFX()` to create client-side flare/light effects and play its siren sound. TASEventManager creates the vanilla `ContaminatedArea_Dynamic` and does not call any GasRaid-specific API, so it is compatible when GasRaidSiren is loaded on the server and clients. The GasRaidSiren archive itself is not part of this working branch.

## Validation limit

Structural delimiter checks passed and the supplied loot JSON parses. This sandbox does not include the DayZ Enforce compiler/Workbench, so these script changes are **not compiled into new PBOs** and require a server-side compile/test before deployment.
