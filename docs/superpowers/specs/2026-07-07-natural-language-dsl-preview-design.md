# Natural Language to DSL Preview Design

## Goal

Let users describe a Blueprint change in natural language, then receive an executable DSL preview before making any Blueprint edits.

The default behavior must stay conservative: generation creates and selects DSL steps, but execution remains a separate user decision through the existing "execute selected steps" flow.

## Current Flow

The plugin already has most of the required pipeline:

- `OnGenerateDslClicked` reads the text box and triages the request.
- `TriageUserIntentForDsl` chooses direct DSL, clarification questions, or a cross-Blueprint plan.
- `StartGenerateDslWithQuestion` sends the natural language request plus Blueprint context to the model.
- `ParseDslStepsFromJson` parses the model response into `CurrentDslSteps`.
- `RebuildDslPreview` displays selectable DSL rows.
- `OnExecuteSelectedDslClicked` validates and executes only after the user clicks the execute button.

This design keeps that architecture and improves the user-facing language around it.

## User Experience

Primary path:

1. User opens the relevant Blueprint.
2. User types a natural language request, for example: "press E to line trace forward and open BP_Door_01".
3. User clicks a generation button with clearer wording, such as "Generate Blueprint Steps" or "Natural Language to DSL".
4. The plugin generates DSL and lists the steps in the existing preview panel.
5. All generated steps are selected by default.
6. The response text tells the user to review the generated steps before executing.
7. User clicks the existing execute button only if the preview looks correct.

If the request is ambiguous, the current clarification flow remains in charge. If it is cross-Blueprint or system-level, the current plan flow remains in charge.

## Non-Goals

- Do not auto-execute immediately after model generation.
- Do not hide the generated DSL preview.
- Do not bypass validation, high-risk confirmation, manual prerequisite warnings, or undo tracking.
- Do not reintroduce local hardcoded DSL templates.
- Do not replace the current DSL schema or executor.

## Design Details

### Entry Point

Keep the existing generation entry point, but update the button text and status copy so users understand that natural language is accepted.

Suggested copy:

- Button: `Generate Blueprint Steps`
- Tooltip: `Generate executable DSL steps from your natural language request. Review before executing.`
- Success message: `DSL generated from natural language. Review the selected steps below, then click Execute Selected Steps if it looks right.`

The existing technical label can still mention DSL in a secondary hint so advanced users know what is being generated.

### Data Flow

The flow remains:

`Natural language request -> triage -> model prompt -> DSL JSON -> parser -> CurrentDslSteps -> preview -> user-triggered execution`

No new execution path is needed for the conservative mode.

### Safety Rules

The design relies on existing safeguards:

- `ValidateSteps` blocks invalid DSL before execution.
- `requiresConfirmation=true` continues to trigger a high-risk confirmation dialog.
- Manual prerequisite summaries continue to warn when variables, functions, or assets may need user setup.
- `Undo Last DSL` remains the rollback mechanism for executed changes.
- Current parse retry and execution retry behavior remains unchanged.

### Settings

No new setting is required for the conservative default.

If a future automatic mode is desired, it should be a separate opt-in setting, disabled by default, and should not be part of this change.

## Implementation Plan

1. Update the primary generation button label and tooltip in `SBlueprintAIAssistantPanel.cpp`.
2. Update the success response copy after `CurrentDslSteps` is populated.
3. Keep `OnExecuteSelectedDslClicked` as the only batch execution trigger.
4. Keep clarification and plan triage unchanged.
5. Compile the UE project plugin.
6. Commit and push the implementation separately from this design commit.

## Validation

Manual validation should cover:

- Simple request: "show Hello from AI on BeginPlay" generates DSL preview and does not execute until clicked.
- Interaction request: "press E to line trace forward and open BP_Door_01" generates DSL preview and does not execute until clicked.
- Ambiguous request: "make an inventory system" still produces clarification or a plan instead of pretending to be a single safe DSL chain.
- High-risk request: a generated high-risk step still requires confirmation before execution.

Build validation:

- Run the UE editor target build for the project after implementation.
- Confirm the plugin loads and the panel shows the updated copy.

