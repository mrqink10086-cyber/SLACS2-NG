ScriptName SLACS_Main extends Quest

Function RegisterEvents()
    UnregisterEvents()
    RegisterForModEvent("SexlabOrgasmSeparate", "OnPPlusOrgasm")
    RegisterForModEvent("HookAnimationStart", "OnPPlusStart")
    RegisterForModEvent("HookAnimationEnd", "OnPPlusEnd")
EndFunction

Function UnregisterEvents()
    UnregisterForModEvent("SexlabOrgasmSeparate")
    UnregisterForModEvent("HookAnimationStart")
    UnregisterForModEvent("HookAnimationEnd")
EndFunction

Event OnPPlusOrgasm(Form akSource, int aiTid)
    NotifyOrgasm(akSource, aiTid)
EndEvent

Event OnPPlusStart(int aiTid, bool abHasPlayer)
    NotifySceneStart(aiTid, abHasPlayer)
EndEvent

Event OnPPlusEnd(int aiTid, bool abHasPlayer)
    NotifySceneEnd(aiTid, abHasPlayer)
EndEvent

Function NotifyOrgasm(Form akSource, int aiTid) Global Native

Function NotifySceneStart(int aiTid, bool abHasPlayer) Global Native

Function NotifySceneEnd(int aiTid, bool abHasPlayer) Global Native
