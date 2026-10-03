# KRLV

**Version 1.0.0** · RLVa API conformance target **2.9.29**

KRLV is the RLVa (Restrained Love Viewer – Advanced) command implementation
for Kirstens S24. It parses `@`-commands from scripted objects, enforces the
resulting restrictions in the viewer, and gives the resident one control
surface to see, manage and revoke them.

---

## Contents

1. [Clean-room statement](#clean-room-statement)
2. [Design principles](#design-principles)
3. [Master switch](#master-switch)
4. [Architecture](#architecture)
5. [Public interface](#public-interface)
6. [Restriction model](#restriction-model)
7. [Command reference](#command-reference)
8. [Safety systems](#safety-systems)
9. [Persistence and integrity](#persistence-and-integrity)
10. [Settings reference](#settings-reference)
11. [S24 Console](#s24-console)
12. [Integration points](#integration-points)
13. [Known limitations](#known-limitations)
14. [Status](#status)

---

## Clean-room statement

KRLV is written entirely from the public RLVa API specification, at
[wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI](https://wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI).
No code from any RLV or RLVa viewer was read, copied or referenced.

With thanks to **Marine Kelley**, creator of the original RestrainedLove API
and viewer, and **Kitty Barnett**, creator of RLVa. The KRLV headers carry this
statement, alongside the MIT license for this code.

---

## Design principles

**Keep restriction logic inside `indra/krlv/`.** Restriction checks in the
wider viewer are a single boolean query per behaviour, such as
`KRlvHandler::isRestricted("fly")`. The rest of the viewer never sees KRLV's
parser, restriction table or persistence.

**Enforce from the idle tick where possible.** A behaviour that can be
enforced entirely from inside the module, such as a camera limit applied every
frame, needs no touchpoint at all. Where a gate is required, it sits at the
one function every entry point already funnels through, not at each UI path.

**Make every touchpoint auditable.** Each reference outside `indra/krlv/` is
marked with the token `KRLV_TOUCHPOINT`. From the `krlv` folder, run:

```
grep -rn "KRLV_TOUCHPOINT" ../newview
```

The table in [Integration points](#integration-points) is the intended
inventory. The grep is the actual one.

**Fail open on storage, fail closed on enforcement.** A missing or damaged
state file is logged and never blocks startup. A restriction that is in force
is always enforced.

---

## Master switch

KRLV is opt-in. Nothing a scripted object sends has any effect until the
resident enables it.

| Item | Value |
|---|---|
| Setting | `KRLVEnabled` (`settings.xml`) |
| Default | **Off** |
| Reader | `KRlvHandler::isEnabled()`, the only place the setting is read |

Every restriction query (`isRestricted()`, `hasRestrictionFrom()`,
`getRestrictionOptions()`) checks `isEnabled()` before reading the table, so
turning KRLV off suspends all restrictions at once, including ones persisted
from an earlier session. Any new restriction-reading entry point must apply
the same guard.

---

## Architecture

```
indra/krlv/
  core/
    krlvcommand.h/.cpp      Parses "@name[:option]=param". Provides a lowercase
                            `behaviour` dispatch key and a case-preserved
                            `behaviourRaw`, used by setdebug_/getdebug_.
    krlvhandler.h/.cpp      The single class the viewer talks to: dispatch,
                            restriction table, master switch, hooks, console.
    krlvrestriction.h       Restriction storage (see Restriction model).
    krlvpersist.h/.cpp      Restriction, blacklist and protected-setting files.
    krlvobjects.h/.cpp      Object whitelist and blacklist, and open/closed mode.
    krlvowners.h/.cpp       Owner list.
    krlvsafeword.h/.cpp     Safeword set, detection and response.
    krlvnotice.h/.cpp       Owner notices with per-kind flood limiting.
    krlvtamper.h/.cpp       Integrity checks with confirmation delay.
    krlvintegrity.h/.cpp    Keyed signatures, atomic writes, read verification.
    krlvautoresponse.h/.cpp Auto-replies to dropped IMs and chat, with cooldown.
    krlvstatus.h            Version strings.
  behaviours/
    krlvbehaviours.h        One registration function per category.
    krlvcamera.h            Camera query functions called by newview.
    krlvteleport.h          Teleport query functions called by newview.
    krlvchat.h              Chat query functions called by newview.
    krlvname.h              Name-tag query functions called by newview.
    krlvattachment.h        Attachment query functions called by newview.
    krlvsharedfolders.h     Shared-folder query functions called by newview.
    krlv*.cpp               One file per category (see Command reference).
```

Built as the `krlv` CMake library target. `newview` links it, and the
`KRLV_*` include directories are exported from `cmake/KRLV.cmake`.

Registration is explicit. `KRlvHandler::init()` calls each
`krlv_register_*_commands()` in a fixed order. There is no static-initializer
self-registration.

---

## Public interface

### Lifecycle

| Method | Purpose |
|---|---|
| `init()` | Registers all categories and starts the idle tick. Loads no account state. |
| `onLoggedIn()` | Clears per-account state, creates the integrity key if missing, loads the per-account files, starts tamper checks. |
| `shutdown()` | Saves restrictions and unregisters the idle tick. |

### Chat and IM

| Method | Purpose |
|---|---|
| `processInboundChat(message, sourceId, ownerId)` | Handles an object's chat command. Returns `true` when the line must not be shown. |
| `processInboundIM(message, fromId)` | Handles the manual IM-only commands (`@version`, `@getblacklist`). |
| `processSelfChatCommand(message, outReply)` | Handles the same manual commands typed into the resident's own chat. |
| `requestSendIm(toId, message)` | Sends a silent IM through the registered send hook. |

### Restrictions

| Method | Purpose |
|---|---|
| `isEnabled()` / `setEnabled(bool)` | Master switch. |
| `isRestricted(behaviour)` | Any active restriction for the behaviour. |
| `hasRestrictionFrom(behaviour, sourceId)` | Restriction imposed by one object. |
| `getRestrictionOptions(behaviour)` | Option strings of an active restriction. |
| `addRestriction` / `removeRestriction` | Mutate the table. Each change is saved and reported to `@notify` subscribers. |
| `clearAllRestrictions()` | Drops every restriction. Used by the safeword and by the Control floater. |
| `clearScriptRestrictions(behaviour)` | Drops one behaviour's script-set restrictions. |
| `hasAnyRestrictionWithPrefix(prefix)` | Used where the specific target is unknown before a call. |

### Blacklist, object lists and protected settings

| Method | Purpose |
|---|---|
| `isBlacklisted` / `addToBlacklist` / `removeFromBlacklist` / `getBlacklist` | Command-name blacklist. Changed through the console only. |
| `getObjectLists()` | Current object lists and mode. |
| `setObjectMode(mode)` | Open or closed. Saved immediately. |
| `addObjectEntry(whitelist, object, owner)` / `removeObjectEntry(...)` | Maintain the lists. Saved immediately. |
| `isDebugSettingProtected` / `addProtectedDebugSetting` / `removeProtectedDebugSetting` / `getProtectedDebugSettings` | Settings protected from `@setdebug=n`. |

### Observability

| Method | Purpose |
|---|---|
| `recentCommands()` | The last 10 commands, with time, source, owner, behaviour and outcome. In memory only. |
| `registerBehaviour` / `registerBehaviourPrefix` | Register a handler for an exact or prefixed command name. |

### Hooks

Newview supplies these at startup, so `krlv` never links viewer code. Each is
set once from `llappviewer.cpp` (or `llstartup.cpp` for the login hook).

| Area | Setters |
|---|---|
| Floaters | `setFloaterCloseHook` |
| Movement and camera | `setForceRotateHook`, `setForceAdjustHeightHook`, `setForceFovHook`, `setGetFovHook` |
| Teleport | `setForceTeleportHook`, `setResolveRegionPositionHook`, `setGetAgentStateHook` |
| Sitting | `setForceSitHook`, `setForceStandHook`, `setForceSitGroundHook`, `setGetSitIdHook` |
| Attachments | `setForceDetachHook`, `setGetOutfitLayersHook`, `setGetAttachPointsHook` |
| Shared folders | `setGetSharedRootHook`, `setListFolderHook`, `setGetParentHook`, `setResolveWornHook`, `setIsSharedItemHook`, `setForceWearBatchHook`, `setForceDetachBatchHook` |
| Group | `setForceSetGroupHook`, `setGetGroupNameHook` |
| Viewer control | `setApplyForcedEnvironmentHook`, `setGetLiveEnvironmentSettingHook` |
| IM | `setSendImHook` |
| Integrity | `setHmacSha256Hook` (see [Persistence and integrity](#persistence-and-integrity)) |

---

## Restriction model

A restriction is identified by a **behaviour key** and a **source object**.

- The table holds one entry per `(behaviour key, source id)` pair.
- Several objects can impose the same restriction. It stays in force until
  every one of them releases it.
- Targeted restrictions such as `@editobj:<UUID>=n` or per-attachment-point
  locks use a dynamic key (for example `attach_point:torso`), so one object can
  lock several targets independently.
- The `_sec` variants of a command share their plain command's key.
- Options such as `@camzoommax:0.5=n` are stored with the entry and read back
  through `getRestrictionOptions()`.

Where a spec states how simultaneous sources combine, the viewer follows it.
For example, `@camzoommax` keeps the smallest value of all active sources.

### Restriction states in the Control floater

| State | Colour | Meaning |
|---|---|---|
| **ACTIVE** | Red | The behaviour is available for restriction. |
| **DISABLED** | Green | The command is ignored: it is blacklisted. |
| **SCRIPT** | Yellow | A script set the restriction. |

A lock indicator is shown while tamper checks are running and the state has
been committed.

---

## Command reference

163 commands are registered across 17 categories. Every command has an
implementation. A few have documented scope limits, listed in
[Known limitations](#known-limitations).

| Category | Commands | Status |
|---|---|---|
| Version Checking (`krlvversion.cpp`) | 3 | Complete. Includes the manual IM-only `@version`. |
| Blacklist (`krlvblacklist.cpp`) | 2 | Complete. Includes both forms of `@getblacklist`. |
| Miscellaneous (`krlvmisc.cpp`) | 5 | Complete. `@permissive` is stored but has no effect (see limitations). |
| Movement (`krlvmovement.cpp`) | 5 | Complete. `@adjustheight` uses a derived formula. |
| Camera and View (`krlvcamera.cpp`) | 25 | Complete, with partial `@camdist*` side effects. |
| Chat, Emotes and IM (`krlvchat.cpp`) | 25 | Complete. |
| Teleportation (`krlvteleport.cpp`) | 12 | Complete. |
| Inventory, Editing and Rezzing (`krlvinventory.cpp`) | 11 | Complete, with `@rez` coverage gap (see limitations). |
| Sitting (`krlvsitting.cpp`) | 4 | Complete. |
| Clothing and Attachments (`krlvattachment.cpp`) | 11 | Complete, with documented simplifications. |
| Shared Folders (`krlvsharedfolders.cpp`) | 29 | Complete, with documented gaps. |
| Touch (`krlvtouch.cpp`) | 11 | Complete. |
| Location (`krlvlocation.cpp`) | 3 | Complete for `@showworldmap` and `@showminimap`. `@showloc` partial. |
| Name Tags and Hovertext (`krlvname.cpp`) | 8 | 7 of 8. `@shownearby` partial. |
| Group (`krlvgroup.cpp`) | 2 | Complete. |
| Viewer Control (`krlvviewercontrol.cpp`) | 6 | Complete. Four are prefix-matched. |
| Unofficial (`krlvunofficial.cpp`) | 1 | Complete. |

### Notes on selected categories

**Chat.** All outbound chat, emote and channel commands are enforced at one
chokepoint in both the nearby-chat and quick-chat send paths. `@sendchat`
implements the full filtering and redirect rule. `@recvchat`, `@recvemote` and
`@recvim` are enforced on inbound messages. `@recvim` drops the message
silently.

**Attachments.** Attach-point and clothing-part locks are keyed per target.
`@detach:<point>=n` locks both directions. `@remattach` locks only detaching.
Force commands pass through the same removal chokepoint as ordinary commands,
so an active restriction is still honoured.

**Shared Folders.** A `#RLV` folder is created in inventory on login and
checked on each idle tick while KRLV is enabled. Folder locks use the folder's
UUID. Force-wear and force-detach go through the already-gated appearance
functions.

**Viewer Control.** `@setdebug_<setting>` and `@getdebug_<setting>` work on any
`gSavedSettings` setting, with a denylist that cannot be changed through
`@setdebug`. `@setenv_<setting>` and `@getenv_<setting>` cover 37 legacy
Windlight parameters. `@setenv` locks every World > Environment entry point.

**Camera.** `@camdrawmin`, `@camdrawmax`, `@camdrawalphamin`,
`@camdrawalphamax` and `@camdrawcolor` run a post-process pass. The pass fogs
the scene toward a substitute colour or texture between the min and max
distances. `@camtextures` uses the same pass with a stencil mask that exempts
attachments.

**Location.** `@showloc` is partially implemented. It replaces the top-bar
parcel text only. Landmark creation, land purchase and the other parts of the
spec are not yet gated.

---

## Safety systems

These systems protect the restriction layer from misuse. They apply to every
restriction, whatever category it belongs to.

### Owners

Owners are trusted avatars who are told when restrictions change.

- Owners are added, removed and changed in the Control floater's
  **Communicate** tab. No PIN is required for owner changes.
- The owner list is saved in `krlv_owners.dat` and signed.
- Owner notices use the flood limiter described below.

### Owner notices and flood limiting

Notices are grouped by kind.

| Kind | Sent when |
|---|---|
| Settings | A KRLV setting or protected debug setting changes. |
| Tamper | A state file is deleted, altered, rolled back or dated in the future. |
| Timestamp | The system clock is inconsistent with stored times. |
| Owner change | The owner list or object lists change. |
| Safeword | The safeword is used. Never rate-limited. |

Each kind has a separate limit. The default interval is **60 seconds**, with
a floor of **5 seconds**, set by `KRLVNoticeIntervalSeconds`. Notices held
back during the interval are summarised in the next one.

### Safeword

The safeword is a phrase of **exactly three words**, in order, set in the
Safeword tab.

- Matching is case-insensitive and ignores punctuation and extra spaces.
- The phrase is stored as a salted hash (`KRLVSafewordHash`,
  `KRLVSafewordSalt`).
- It is checked in nearby chat, in IMs and in quick chat, and each of those
  messages is still sent.
- When the safeword is detected, KRLV clears every restriction, turns the
  master switch off and notifies all owners.

### Tamper detection

State files are checked against their signatures and against stored times.

- The first check runs five seconds after login.
- An incident is confirmed only if it persists for **60 seconds**.
- Clock differences up to **300 seconds** are tolerated.
- Each confirmed incident is reported once, recorded in
  `KRLVTamperNotifiedKeys`.
- Incident types: file deleted, signature failed, rollback detected, timestamp
  in the future.

### Object lists

The object lists decide which source objects are heard.

| Mode | Behaviour |
|---|---|
| **Open** (default) | Every object is heard, except those on the blacklist. |
| **Closed** | Only objects on the whitelist are heard. |

- An entry matches only when the **object UUID and the owner UUID both match**.
  A re-rezzed copy has a new object UUID and needs a new entry.
- An object is on one list at a time. A blacklist match always takes priority.
- Closed mode requires an explicit confirmation before it can be applied.
- A refused command is logged with its reason and is not shown in chat.
- Entries are managed from the **Objects** tab, or from the **Auto-reply** tab
  using the selected command's source.

### Auto-reply

Dropped IMs (`@recvim`) and dropped chat (`@recvchat`) can receive an
automatic reply.

- Replies are sent once per sender per cooldown period.
- The cooldown is set by `KRLVAutoResponseCooldownSeconds`, with a floor of
  60 seconds.
- Chat replies are sent as IMs, and only to avatars.
- The reply texts are `KRLVAutoResponseIMText` and `KRLVAutoResponseChatText`.

### Command log

The **Auto-reply** tab shows the last 10 commands: time, source object, command,
and outcome (`received`, `refused: blacklisted`, `refused: object blacklisted`
or `refused: object not whitelisted`).

### Protected settings

`@setdebug=n` restricts editing of the settings in KRLV's own protected list.
The list starts empty. The Debug Settings floater refuses changes to protected
settings and notifies the owners. `@setdebug_<setting>` cannot set `KRLVEnabled`
or `KRLVRestrictionsWereActive`, so an object cannot switch KRLV off or clear its
own tamper marker.

### KRLV Control floater

The floater is the only interface for managing KRLV.

| Requirement | Detail |
|---|---|
| Maturity | Opens only when the viewer's maturity preference is **Adult**. Closes if that changes while open. |
| PIN | Required on every open. Set on first use. |
| Auto-lock | Re-locks after `KRLVControlLockSeconds` of minimised time, or on close. `0` means never. |
| Warning | An always-visible banner states what RLV can do. |

Tabs: **Control**, **Auto-reply**, **Restrictions**, **Objects**,
**Communicate**, **Safeword**, **Security**.

---

## Persistence and integrity

### State files

All files are stored in the per-account directory (`LL_PATH_PER_SL_ACCOUNT`).

| File | Contents | Signed |
|---|---|---|
| `krlv_restrictions.dat` | Active restriction table | Yes |
| `krlv_blacklist.dat` | Command-name blacklist | Yes |
| `krlv_protected_debug.dat` | Protected debug settings | Yes |
| `krlv_owners.dat` | Owner list | Yes |
| `krlv_objects.dat` | Object whitelist, blacklist and mode | Yes |

Restrictions are saved on every change and again on shutdown. All of these
files live in the per-account directory, which exists only after login, so they
are loaded in `onLoggedIn()`, not `init()`. Login clears the in-memory state
first, so each account sees only its own files.

### Signatures

- Each file carries a signature over its contents, computed with
  **HMAC-SHA256** under a per-install key (`KRLVIntegrityKey`). Each file uses
  its own domain string, so a signature from one file is not valid in another.
- A file whose signature scheme is not `hmac-sha256` is treated as tampered.
  It is discarded and logged as an integrity incident. There are no legacy
  files to migrate.
- Nothing is written unsigned. If no HMAC hook is registered, the save fails
  and is logged.
- Writes are atomic at the rename: the new file is written to a temporary name
  and then renamed over the old one. The data is not fsynced, so a power loss
  during a write can leave a partial file, which the loader treats as corrupt.
- The per-install key is created at login, so it is saved with settings.

### Limits

Tamper-evidence is not a security boundary. A resident with access to their own
account folder can delete any file. KRLV's response to that is to log it, notify
owners, and treat the restriction as ended. It cannot stop the deletion.

Every failure is logged and never blocks startup. A missing or damaged file
means the affected restriction is treated as inactive.

---

## Settings reference

| Key | Purpose | Default |
|---|---|---|
| `KRLVEnabled` | Master switch. | Off |
| `KRLVControlEnabled` | The Control floater's enable checkbox. Cleared by the safeword. | Off |
| `KRLVControlPinHash`, `KRLVControlPinSalt` | PIN verification. | Unset |
| `KRLVControlLockSeconds` | Auto-lock after minimised time. `0` = never. | Set by user |
| `KRLVNoticeIntervalSeconds` | Owner-notice interval. Floor 5. | 60 |
| `KRLVSafewordHash`, `KRLVSafewordSalt` | Safeword verification. | Unset |
| `KRLVIntegrityKey` | Per-install signing key. Generated on first run. | Generated |
| `KRLVOwnersWrittenEpoch` | Time of last owner-list write, for rollback checks. | Set on save |
| `KRLVOwnersWereSet` | Records that owners have existed. | Set on save |
| `KRLVRestrictionsWereActive` | Records that restrictions were in force at last shutdown. | Set on save |
| `KRLVTamperNotifiedKeys` | Incidents already reported. | Empty |
| `KRLVAutoResponseIMText` | Auto-reply text for IMs. | Set by user |
| `KRLVAutoResponseChatText` | Auto-reply text for chat. | Set by user |
| `KRLVAutoResponseCooldownSeconds` | Per-sender cooldown. Floor 60. | Set by user |

Settings are saved through the standard settings path. KRLV does not write
`settings.xml` directly.

---

## S24 Console

```
krlv                              Show status.
krlv on | off                     Set the master switch.
krlv blacklist                    List blacklisted command names.
krlv blacklist add <name>         Blacklist a command name.
krlv blacklist remove <name>      Remove a command name from the blacklist.
krlv protectdebug                 List protected debug settings.
krlv protectdebug add <name>      Protect a debug setting from @setdebug=n.
krlv protectdebug remove <name>   Remove a protected debug setting.
```

Console commands are the only way to change the blacklist or the protected
settings. No object command can change either.

---

## Integration points

Each row is a point where code outside `indra/krlv/` refers to KRLV. The
`KRLV_TOUCHPOINT` comment marks each one.

### Lifecycle and messaging

| File | Function | Purpose |
|---|---|---|
| `llappviewer.cpp` | Startup | `KRlvHandler::init()`, and every hook setter. |
| `llappviewer.cpp` | Shutdown | `KRlvHandler::shutdown()`. |
| `llstartup.cpp` | Login | `onLoggedIn()`, which starts tamper checks. |
| `llviewermessage.cpp` | `process_chat_from_simulator()` | Inbound object chat, `@recvchat`, `@recvemote`, `@shownames` name substitution. Dropped chat triggers auto-reply. |
| `llimprocessing.cpp` | `IM_NOTHING_SPECIAL` | Manual IM commands, `@recvim`, and auto-reply to dropped IMs. |
| `llfloaterimnearbychat.cpp` | `sendChat()` | Outbound chat gate, self-chat commands, safeword check. |
| `kvfloaterquickchat.cpp` | `sendChat()` | Same gate for quick chat, plus safeword check. |
| `llimview.cpp` | `LLIMModel::sendMessage()` | `@sendim` family, and safeword check. |
| `llimview.cpp` | `LLIMMgr::addSession()` | `@startim` family. |
| `kvdebugconsole.cpp` | `krlv` command | Console passthrough. |

### Behaviour gates

| File | Function | Commands gated |
|---|---|---|
| `llagent.cpp` | `canFly()` | `@fly` |
| `llagent.cpp` | `setAlwaysRun()` | `@alwaysrun` |
| `llviewerinput.cpp` | `agent_handle_doubletap_run()` | `@temprun` |
| `llviewercamera.cpp` | `setDefaultFOV()` | Camera zoom and FOV bounds |
| `llagentcamera.cpp` | `calcCameraPositionTargetGlobal()` | Camera distance bounds |
| `llagentcamera.cpp` | `setFocusOnAvatar()` | `@camunlock` |
| `llvoavatar.cpp` | `isTooComplex()` | `@camavdist` |
| `lldrawpool.cpp` | `pushBatch()` | `@camtextures` stencil tagging |
| `dxdrawpoolalpha.cpp` | `renderAlpha()` | `@camtextures` stencil tagging |
| `dxpipeline.cpp` | `renderGeomDeferred()` | `@camtextures` per-frame check |
| `dxpipeline.cpp` | `presentDeferredScreen()` | Camera fog and texture pass |
| `llagent.cpp` | `teleportViaLandmark()`, `teleportViaLocation()`, `teleportRequest()` | `@tplm`, `@tploc`, `@tplocal` |
| `llviewermenu.cpp` | `handle_object_sit()` | `@sit`, `@sittp`, `@interact` |
| `llagent.cpp` | `standUp()`, `sitDown()` | `@unsit`, `@sitground` |
| `llimprocessing.cpp` | `IM_LURE_USER`, `IM_TELEPORT_REQUEST` | `@tplure`, `@tprequest`, `@accepttp` |
| `llgesturemgr.cpp` | `playGesture()` | `@sendgesture` |
| `llviewermenu.cpp` | `handle_object_edit()` | `@edit` family, `@interact` |
| `llviewermenu.cpp` | `handle_object_delete()` | `@rez`, `@interact` |
| `lltooldraganddrop.cpp` | `dropObject()` | `@rez`, `@interact` |
| `llgiveinventory.cpp` | `doGiveInventoryItem()`, `doGiveInventoryCategory()` | `@share` family |
| `llviewermenu.cpp` | `LLAttachmentDetach`, `LLAttachmentDetachFromPoint` | `@detach`, `@remattach` |
| `llinventorybridge.cpp` | `rez_attachment()` | `@addattach`, `@detach:<point>` |
| `llappearancemgr.cpp` | `wearItemsOnAvatar()` | `@addoutfit`, `@sharedwear`, `@unsharedwear` |
| `llappearancemgr.cpp` | `removeItemsFromAvatar()` | `@remoutfit`, `@sharedunwear`, `@unsharedunwear` |
| `llviewermessage.cpp` | `process_script_question()` | `@acceptpermission`, `@denypermission` |
| `llvoavatar.cpp` | `idleUpdateNameTag()` | `@shownames`, `@shownametags` |
| `lltoolpie.cpp` | `handleTooltipObject()` | `@shownames` tooltip |
| `llnetmap.cpp` | `handleToolTipAgent()` | `@shownames`, `@shownearby` tooltip |
| `llhudtext.cpp` | `renderText()` | `@showhovertext` family |
| `llpaneltopinfobar.cpp` | `setParcelInfoText()` | `@showloc` |
| `lltoolgrab.cpp` | `send_ObjectGrab_message()` | Touch family |
| `llgroupactions.cpp` | `activate()` | `@setgroup` |
| `llappviewer.cpp` | `idle_afk_check()` | `@allowidle` |
| `llviewermenu.cpp` | `LLWorldEnvSettings::handleEvent()` | `@setenv` |
| `llfloatersettingsdebug.cpp` | `onCommitSettings()`, `onClickDefault()` | `@setdebug` |

### Floater gate

Floater-gated behaviours (`@showinv`, `@showworldmap`, `@showminimap`, and
others) are polled every idle tick, not checked at each entry point. A
restricted floater that is visible is closed through `setFloaterCloseHook`.

---

## Known limitations

These are documented scope limits. Each is a deliberate choice, not an
unnoticed defect.

**Commands and targets**

- `@clear`, `@getstatus` and `@getstatusall` identify restrictions by the
  object that imposed them. Per-target restrictions such as `@editobj:<UUID>`
  are keyed by the target, so `@clear` from the imposing object does not clear
  them.
- `@permissive=<y/n>` is stored and reported, but has no effect. The `_sec`
  variants do not yet distinguish same-object exceptions.
- `_sec` variants (`@recvchat_sec`, `@recvemote_sec`, `@sendim_sec`,
  `@recvim_sec`, `@tplure_sec`, `@tprequest_sec`) share their plain command's
  key, so the same-object-only rule for their exceptions is not enforced.
  Exceptions for the plain commands are stored per imposing object.
- `@sendchannel` implements the blanket block only. The per-channel exception
  form is parsed and ignored.

**Camera and view**

- `@camdistmax` and `@camdistmin` apply the distance clamp only. The Mouselook
  side effects are not implemented.
- `@camdraw*` substitutes a screen-space pattern, not the restricted object's
  own UV. Fog and substitute pattern are visible, but not mapped to each object's
  surface.

**Teleport and messaging**

- `@tprequest` declines silently. No decline notice is sent.
- `@recvim` drops the IM silently. The sender is not notified.

**Location and names**

- `@showloc` gates the top-bar parcel text only. Landmark creation, land purchase,
  the About Land box and chat obfuscation are not gated.
- `@shownearby` gates the minimap tooltip only. The People window's Nearby list is
  shared infrastructure, and gating it would also censor other tabs.
- The spec's pie-menu name clause has no chokepoint to gate.

**Inventory and attachments**

- `@rez` covers rez-from-inventory, attachment dropping and deletion. Object creation
  from the build palette is not confirmed to route through the same gate.
- `@getoutfit` and `@getattach` return digits in the viewer's own enumeration order,
  not the RLVa documented order.
- `@remoutfit=force` with no part is not implemented. A multi-layer clothing type
  removes only its base layer.
- `@detachthis`, `@attachthis` and their `allthis` and `_except` forms are implemented
  by analogy with `@detach:<point>`, because the source text is incomplete.
- `@getinvworn` replies in the same format as `@getinv`, matching the spec text.
- `@getpathnew` returns a single path, the same as `@getpath`.
- The `~` no-strip folder marker is not implemented.

**Viewer control**

- `@setenv_` covers 37 of the spec's parameters. `preset` and the intensity
  colour components are reported through the stub notice. Two parameters are
  documented approximations (`daytime`, `sunglowfocus`/`sunglowsize`).
- `@setdebug` protects only the settings in KRLV's own list, which the user
  maintains through the console. The spec's internal list is not public.

**Unofficial**

- `@allowidle` applies to the automatic away timer only, not to minimise-triggered
  away.

---

## Status

| Item | Value |
|---|---|
| Version | 1.0.0 |
| RLVa API target | 2.9.29 |
| Commands registered | 163 |
| Categories | 17 |
| Integration points | Listed above; audit with `grep -rn "KRLV_TOUCHPOINT" ../newview` |

**Public-release status.** KRLV is shipping as a public feature, so the
community's experience of it is the main source of feedback. Reports of
incorrect behaviour, unexpected refusals or unclear wording should be made
through the standard bug-report channel.
