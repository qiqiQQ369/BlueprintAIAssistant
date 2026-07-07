# Blueprint AI Assistant - INSTALL

Version: 0.1.0 | Engine: UE 5.6 | Platform: Win64 Editor
Packaged: 20260706-163236

## Level designers
1. Close Unreal Editor.
2. Unzip to project root (same folder as .uproject). Overwrite all.
3. Open project, enable plugin, restart editor.
4. Project Settings - Plugins - Blueprint AI Assistant: API key.
5. Window - Blueprint AI Assistant.

See Plugins/BlueprintAIAssistant/DISTRIBUTION.md
See docs/BlueprintAIAssistant_LDGuide.md

## Programmers
- Source: Plugins/BlueprintAIAssistant/Source/
- Tests: Plugins/BlueprintAIAssistant/Tests/SmokeDsl/
- Build: scripts/build-uecpp.ps1
- Smoke: scripts/run-blueprint-ai-smoke.ps1 -BuildFirst
