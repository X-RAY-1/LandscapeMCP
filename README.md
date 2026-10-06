# LandscapeMCP

Unreal Engine 5.8の公式Model Context ProtocolへLandscape操作を追加するEditor Pluginです。AI Agentが明示したLandscapeへ、小規模な作成・高度取得・Heightfield編集と、傾斜・領域高度・歩行可能性の読み取り評価を実行する基盤を提供します。

独立MCP Serverは実装せず、`ToolsetRegistry` → `ModelContextProtocol` の公式機構へ追加Toolsetとして登録します。Toolset名は `LandscapeMCP.LandscapeMCPToolset`、Toolset Versionは `0.2`、Plugin descriptorの`VersionName`も`0.2`です。初版baselineは **v0.1.0** で、v0.2は既存5 Toolのschema・挙動・安全境界を変えずに読み取り専用の地形評価Toolを3つ追加します。

## 対応環境

Freeze時点で確認した環境はUnreal Engine 5.8、Windows 11、Win64 Editorです。他のEngineバージョン・OS・Targetは未検証です。Runtime／Cooked向けModuleやContentは含みません。

## Tool一覧

| Tool | 概要 |
|---|---|
| `CreateLandscape` | 指定した読み込み済みLevelへ平坦なLandscapeを作成 |
| `GetHeight` | 指定World XYの元Heightfieldをbilinear補間して高度取得 |
| `SculptRegion` | 円形範囲を盛り上げる／掘り下げる |
| `SmoothRegion` | 円形範囲へ3x3近傍平均を1回適用 |
| `FlattenRegion` | 円形範囲を指定World高度へ近づける |
| `GetSlope` | 指定World XYの傾斜角・最大上昇方向・法線を取得（v0.2、読み取り専用） |
| `GetHeightRegion` | 矩形領域の高度を格子でまとめて取得し、最小・最大・平均を集計（v0.2、読み取り専用） |
| `EvaluateWalkability` | 指定した歩行可能角に対する傾斜の評価（v0.2、読み取り専用） |

入力・出力・単位は[Tool仕様](docs/tools.md)を参照してください。

## 安全設計

- 書き込みToolの`bDryRun`は既定`true`。Validationと変更計画だけを実行します。
- 対象は完全Object Pathで指定し、名前やラベルから推測しません。
- 数値・範囲・対象構成を検証し、非対応構成は明示的にFAILします。
- Editor game thread限定。PIE、Save、GC中は読み取りも拒否します。
- 実編集はTransaction／Undo対応で、結果照合失敗時はUndo rollbackを試みます。
- v0.2の評価Tool（`GetSlope`／`GetHeightRegion`／`EvaluateWalkability`）は読み取り専用で、Transaction・Modify・Package dirty化を行いません。Landscape端ではclipせずFAILします。
- `EvaluateWalkability`はLandscape形状の評価です。歩行可能角は呼び出し側が渡し、CharacterやBlueprintを参照しません。
- 保存・Autosave APIを呼びません。Editorの独立Autosaveは別途管理してください。実編集によるPackage dirty化は行われます。

詳細は[安全境界](docs/safety.md)を参照してください。

## インストール

1. Editorを終了してから、本リポジトリをProjectの`Plugins/LandscapeMCP`へ配置します。
2. Projectで公式`ToolsetRegistry`と`ModelContextProtocol`を有効にします。descriptorは`ToolsetRegistry`依存を宣言し、MCP公開には別途`ModelContextProtocol`が必要です。
3. UE5.8と対応C++ Build環境でPluginをBuildします。BinariesはGit管理していません。
4. Editorを起動し、MCP Clientから`list_toolsets`／`describe_toolset`で検出します。All ToolsetsまたはLandscapeMCP Toolsetを有効にしてください。

追加MCP Server、PCG、Modeling、Scriptable Toolsは本Pluginの依存ではありません。Build Module依存はCore、CoreUObject、Engine、ToolsetRegistry、Landscape、UnrealEd、Foliage、RenderCore、Json、JsonUtilitiesです。

Build・Automationの手順は[開発手順](docs/development.md)を参照してください。

## 使用例

公式`call_tool`を使用します。まず新規の未保存・非World Partition Levelを開き、完全ULevel Object Pathを取得します。以下のパスは例なので実際の値へ置き換えてください。

```json
{
  "name": "call_tool",
  "arguments": {
    "toolset_name": "LandscapeMCP.LandscapeMCPToolset",
    "tool_name": "CreateLandscape",
    "arguments": {
      "levelPath": "/Temp/Untitled_1.Untitled:PersistentLevel",
      "landscapeName": "AI_LandscapeTest",
      "location": {"x": 0, "y": 0, "z": 0},
      "scale": {"x": 100, "y": 100, "z": 100},
      "componentCountX": 1, "componentCountY": 1,
      "sectionsPerComponent": 1, "quadsPerSection": 63,
      "initialWorldHeight": 0, "bDryRun": true
    }
  }
}
```

Validation成功後だけ同じ入力で`bDryRun:false`として実行し、返された`landscapePath`を次の`SculptRegion`と`GetHeight`へ渡します。`SculptRegion`もdry-runを先に実行してください。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","center":{"x":3150,"y":3150},"radiusCm":800,"strengthCm":100,"falloff":1,"bRaise":true,"bDryRun":true}
```

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","worldX":3150,"worldY":3150}
```

編集後の斜面は、歩行させる前に評価できます。`walkableFloorAngleDeg`には対象CharacterのWalkable Floor Angleを呼び出し側で調べて渡します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","worldX":2700,"worldY":3150,"walkableFloorAngleDeg":44.77,"sampleDistanceCm":100}
```

エラー時は依存する後続処理を停止します。UndoはEditorの既存Undoを使用します。

## 既知の制限

World Partition、Landscape Streaming Proxy、複雑なEdit Layer、Nanite Landscape、付属Foliage、Visibility Holeに非対応です。path探索、path brush／rectangle brush、不可歩行領域の自動Smoothは未実装です。`EvaluateWalkability`はCharacterMovementの完全な再現ではありません。[制限一覧](docs/limitations.md)を参照してください。

## Unreal MCP側の既知事項

公式`StartPIE`が`PIE ended before warmup completed.`を返しながら、PIE自体が開始しているケースを確認しています。**LandscapeMCPの既知不具合とは分類していません。** 起動Toolを無条件retryせず、`IsPIERunning`、PIE World、PlayerController、Characterの状態を照合してください。LandscapeMCPの8 ToolはPIE中に使用できません。

## 検証

Freeze判定は **`LandscapeMCP v0.1: FREEZE CANDIDATE`**。Automation、MCP編集E2E、通常入力のGoal完走、Boundary往復、急斜面の正常な阻止と逆入力復帰を確認しました。[検証要約](docs/testing.md)に範囲・数値・外部制約を記録しています。v0.2で追加した3 ToolのBuild・Automation・簡易MCP E2Eの結果も同じ文書に記録しています。Characterの実走行による確認は別途行います。

## License

Licenseは未指定です。このリポジトリは独自のLICENSEをまだ付与していません。利用・配布条件は権利者の判断が必要です。Unreal Engineと公式Pluginの利用条件は各提供元の規約に従います。
