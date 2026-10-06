# LandscapeMCP

Unreal EngineのLandscapeを、AI Agent / MCPクライアントから作成・編集・解析するためのEditor Toolsetです。

Unreal Engine 5.8の公式Model Context Protocol（`ToolsetRegistry` → `ModelContextProtocol`）へ、追加Toolsetとして登録されるEditor Pluginです。独立したMCP Serverは含みません。

- Toolset名: `LandscapeMCP.LandscapeMCPToolset`
- Toolset Version: `0.3`
- 現在のリリース: v0.3.0（実験的。Plugin descriptorでも`IsExperimentalVersion`を指定しています）
- 配布形態: ソースのみ。Build済みのBinariesは配布していません

## 概要

AI AgentがLandscapeを扱うときに必要な「作る → 編集する → 測る → 歩行可能性を評価する」の一連を、範囲と対象を限定したToolとして提供します。

対象は明示された1つのLandscapeだけです。名前やラベルからの推測、対象がない場合の自動作成、保存は行いません。対応していない構成のLandscapeは、編集も読み取りも拒否します。対応範囲は[Limitations](#limitations)を先に確認してください。

## 主な機能

- **作成**: 読み込み済みのLevelへ、小規模で平坦なLandscapeを作成
- **編集**: 円形範囲の盛り上げ・掘り下げ、平滑化、指定高度への平坦化。Dry-runとUndoに対応
- **高さの取得**: 1点、または矩形領域をまとめて取得
- **傾斜の解析**: 指定位置の代表的な傾斜と向き、周囲の実三角形から求めた局所的な最大・平均傾斜
- **歩行可能性の評価**: 呼び出し側が渡した歩行可能角に対する、1点または領域のgeometry評価

## Tool一覧

| Tool | 種別 | 概要 |
|---|---|---|
| `CreateLandscape` | 書き込み | 指定した読み込み済みLevelへ平坦なLandscapeを作成 |
| `GetHeight` | 読み取り | 指定World XYの高度を取得（元Heightfieldのbilinear補間） |
| `SculptRegion` | 書き込み | 円形範囲を盛り上げる／掘り下げる |
| `SmoothRegion` | 書き込み | 円形範囲へ3x3近傍平均を1回適用 |
| `FlattenRegion` | 書き込み | 円形範囲を指定World高度へ近づける |
| `GetSlope` | 読み取り | 指定World XYの傾斜角・最大上昇方向・法線を取得 |
| `GetHeightRegion` | 読み取り | 矩形領域の高度を格子で取得し、最小・最大・平均と最大傾斜を集計 |
| `EvaluateWalkability` | 読み取り | 指定位置の傾斜を、渡された歩行可能角と比較 |
| `AnalyzeSlopeNeighborhood` | 読み取り | 中心傾斜と、周囲の実三角形から求めた局所最大・平均傾斜 |
| `EvaluateWalkabilityRegion` | 読み取り | 矩形領域の実三角形を、渡された歩行可能角に対して一括評価 |

入力・出力・単位・計算式は[Tool仕様](docs/tools.md)にあります。距離・高さ・座標はワールドcm、角度は度です。

### 中心傾斜と局所最大傾斜

傾斜には2種類の値があり、用途が違います。

| 値 | 求め方 | 向いている用途 | 弱点 |
|---|---|---|---|
| 中心傾斜（`GetSlope`の`slopeDegrees`、`centerSlopeDegrees`） | 中心から`sampleDistanceCm`離れた4点の中心差分 | その場所の代表的な傾きと向き | 対称な地形で打ち消し合う。丘の頂点・尾根・谷底・鞍部は周囲が急でも0度になる |
| 局所最大傾斜（`localMaxSlopeDegrees`、`maxTriangleSlopeDegrees`、`EvaluateWalkabilityRegion`の`maxSlopeDegrees`） | Landscapeの実三角形ごとの傾斜の最大値 | 「この周辺に急な面があるか」の検出 | 1枚でも急な三角形があれば大きくなる。向きは最大の三角形のものだけ |

歩けるかどうかを見るときは、中心傾斜だけで判断せず、`AnalyzeSlopeNeighborhood`か`EvaluateWalkabilityRegion`で周囲の実三角形も確認してください。

## Requirements

Tested:

- Unreal Engine 5.8 / Windows 64-bit（Win64 Editor）

必要なもの:

- Unreal Engine 5.8のEditor。本PluginはEditor専用で、Runtime／Cooked向けのModuleやContentは含みません
- Unreal Engine側のModel Context Protocol環境。公式Pluginの`ToolsetRegistry`（本Pluginのdescriptorが依存を宣言）と`ModelContextProtocol`（MCPとして公開するために必要）
- UE5.8に対応したWin64のC++ Build環境。本Pluginはソースで配布しており、利用する側でのBuildが必要です

他のEngineバージョン、OS、Targetは検証していません。動作は保証しません。

Build Moduleの依存はCore、CoreUObject、Engine、ToolsetRegistry、Landscape、UnrealEd、Foliage、RenderCore、Json、JsonUtilitiesで、すべてEngine付属です。第三者ライブラリは含みません。

## Installation

本Pluginはソースで配布しています。このリポジトリにも、GitHub ReleaseにもBuild済みのBinariesは含まれません。利用するには、手元のUnreal Engine 5.8でBuildします。

最終的な配置は次のとおりです。

```
<Project>/Plugins/LandscapeMCP/
├── LandscapeMCP.uplugin
├── Source/
└── Binaries/        （Buildで生成される）
```

### ソースからBuildする

1. このリポジトリをcloneまたはdownloadします。
2. `RunUAT BuildPlugin`でBuildします。出力先には、リポジトリの外の新しいフォルダを指定します。

   ```powershell
   & '<UE_ROOT>/Engine/Build/BatchFiles/RunUAT.bat' BuildPlugin `
     '-Plugin=<REPO_ROOT>/LandscapeMCP.uplugin' `
     '-Package=<NEW_PACKAGE_DIR>' -TargetPlatforms=Win64 -NoP4
   ```

3. Unreal Editorを終了し、出力フォルダの中身（`Intermediate`を除く）を`<Project>/Plugins/LandscapeMCP/`へ置きます。
4. Editorを起動します。

検証したのはこの手順です。C++ Projectであれば、`Plugins/LandscapeMCP`へソースを置いてProjectと一緒にBuildする通常の方法も使えるはずですが、この方法は検証していません。

BuildしたBinariesは、Buildに使ったEngineでだけ読み込めます。Engineを更新した場合はBuildし直してください。

### Pluginの有効化と検出の確認

1. Projectで公式Pluginの`ModelContextProtocol`を有効にします。`ToolsetRegistry`は本Pluginの依存として有効になります。
2. Model Context Protocol側で、All ToolsetsまたはLandscapeMCPのToolsetを有効にします。
3. MCPクライアントをEditorのMCP Serverへ接続します。接続先はModel Context Protocol Pluginの設定に従ってください。検証環境では`http://127.0.0.1:8000/mcp`（HTTP）でした。
4. `list_toolsets`に`LandscapeMCP.LandscapeMCPToolset`が現れ、`describe_toolset`でVersion `0.3`と10 Toolが返ることを確認します。

PCG、Modeling、Scriptable Toolsなど、他のPluginは本Pluginの依存ではありません。

## MCPからの利用方法

公式MCP Serverが公開するのは`list_toolsets`、`describe_toolset`、`call_tool`の3つです。本PluginのToolは、`call_tool`に`toolset_name`と`tool_name`を指定して呼び出します。

```json
{
  "name": "call_tool",
  "arguments": {
    "toolset_name": "LandscapeMCP.LandscapeMCPToolset",
    "tool_name": "GetHeight",
    "arguments": {
      "landscapePath": "/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest",
      "worldX": 3150,
      "worldY": 3150
    }
  }
}
```

以下の例では、`arguments`の中身だけを示します。パスは例なので、実際の値へ置き換えてください。

### 例: 作る → 編集する → 測る → 歩行可能性を評価する

前提として、World Partitionを使わないLevelをEditorで開き、そのULevelの完全Object Pathを用意します。

**1. 作る（`CreateLandscape`）**

まずDry-runで検証します。

```json
{"levelPath":"/Temp/Untitled_1.Untitled:PersistentLevel","landscapeName":"AI_LandscapeTest","location":{"x":0,"y":0,"z":0},"scale":{"x":100,"y":100,"z":100},"componentCountX":1,"componentCountY":1,"sectionsPerComponent":1,"quadsPerSection":63,"initialWorldHeight":0,"bDryRun":true}
```

成功したら、同じ入力を`"bDryRun":false`にして実行します。64×64 Sample、63m四方の平坦なLandscapeができます。返された`landscapePath`を以降のToolへ渡します。

**2. 編集する（`SculptRegion`）**

丘を作ります。これもDry-runで変更量を確認してから、`"bDryRun":false`で実行します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","center":{"x":3000,"y":3000},"radiusCm":600,"strengthCm":600,"falloff":1,"bRaise":true,"bDryRun":true}
```

**3. 測る（`GetSlope`、`AnalyzeSlopeNeighborhood`）**

丘の斜面の傾斜と向きを取得します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","worldX":3300,"worldY":3000,"sampleDistanceCm":100}
```

丘の頂点では`GetSlope`が0度になります（両側の斜面が打ち消し合うため）。周囲の急な面は`AnalyzeSlopeNeighborhood`で確認します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","worldX":3000,"worldY":3000,"radiusCm":300,"sampleDistanceCm":100}
```

この例では`centerSlopeDegrees`が0、`localMaxSlopeDegrees`が約58度になります。

**4. 歩行可能性を評価する（`EvaluateWalkabilityRegion`）**

丘を含む一帯を、歩行可能角44.77度で評価します。`walkableFloorAngleDeg`には、対象CharacterのWalkable Floor Angleを呼び出し側で調べて渡します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_LandscapeTest","minX":2400,"minY":2400,"maxX":3600,"maxY":3600,"walkableFloorAngleDeg":44.77}
```

この例では`classification`が`MIXED`になり、`walkableCount`／`unwalkableCount`／`worstLocation`から、どこが急すぎるかが分かります。結果を見て`SmoothRegion`や`FlattenRegion`で直し、もう一度評価する、という使い方を想定しています。

### 呼び出しの原則

- 書き込みToolは、Dry-runで結果を確認してから本実行します。
- エラーが返ったら、それに依存する後続の処理を止めます。別の対象を推測して続行しないでください。
- 編集の取り消しには、Editorの通常のUndoを使います。
- PIE中は10 Toolすべてが使えません。

## Safety design

誤った対象を編集しないこと、気付かないうちに状態を変えないことを優先した設計です。ただし、これは被害の範囲を限定するための仕組みで、結果の正しさを保証するものではありません。

- **完全Object Path**: 対象は読み込み済みActor／Levelの完全Object Pathで指定します。名前やラベルから推測しません。
- **Dry-run**: 書き込みToolの`bDryRun`は既定で`true`です。Dry-runは同じValidationと変更計画を実行し、Heightfield・Package・Undo履歴を変更しません。
- **Undo**: 実編集はTransactionとして記録され、EditorのUndoで戻せます。書き込み後の照合に失敗した場合は、Undoによるrollbackを試みます。
- **保存しない**: 保存・Autosave APIを呼びません。ただし実編集はPackageをdirtyにします。Editor自身のAutosaveは本Pluginの管理外です。
- **read-onlyの分析Tool**: `GetHeight`、`GetSlope`、`GetHeightRegion`、`EvaluateWalkability`、`AnalyzeSlopeNeighborhood`、`EvaluateWalkabilityRegion`は、Transaction・Modify・Package dirty化を行いません。
- **PIE / Save / GC中は拒否**: Editorのgame thread上、現在のEditor Worldだけを対象とし、PIE中・Package保存中・GC中は読み取りも拒否します。
- **fail-closed**: 入力や対象が不明・不正な場合は実行せずにFAILします。NaN／Infinity、範囲外、逆転した範囲を拒否します。
- **Sample上限**: 作成は262144 Sample、編集は16384 Sample、領域の高さ取得は1024 Sample、実三角形の解析は16384頂点まで。半径・強さ・距離にも上限があります。
- **非対応のLandscapeは拒否**: 下の[Limitations](#limitations)にある構成は、編集だけでなく読み取りも拒否します。

詳細は[安全境界](docs/safety.md)にあります。

## Limitations

- **対応していない構成**: World Partition、external actorを使うLevel、Landscape Streaming Proxy、複数または特殊なEdit Layer、Blueprint brush、Nanite Landscape、回転したLandscape、付属Foliage、Visibility Holeには対応していません。これらはFAILします。
- **CharacterMovementの完全な再現ではない**: 歩行可能性の評価は、Landscape形状の傾斜と、渡された歩行可能角の比較だけです。Capsule、Step Height、Perch、速度などは扱いません。`WALKABLE`は実際に歩けることを、`UNWALKABLE`は実際に進めないことを保証しません。
- **`NEAR_LIMIT`は量子化誤差だけ**: `NEAR_LIMIT`と`slopeUncertaintyDegrees`は、高さの16-bit量子化に対する測定上の区分です。Collisionとの差やCharacter側の要因による不確実性は含みません。
- **Collision LOD / Simple Collision Mipは考慮しない**: 実三角形の解析はHeightfieldの解像度で行います。Collisionの解像度を粗くしたLandscapeでは、実際の接触面と一致しません。
- **三角形分割はUE5.8で検証**: セルを対角線00-11で分割する前提は、UE5.8のEngineソースとCollisionのline traceで確認したものです。同じ確認をAutomationに入れています。
- **別のUEバージョンは未検証**: UE5.8 / Win64 Editor以外では検証していません。
- **高さはCollisionそのものではない**: `GetHeight`などの高さは元Heightfieldのbilinear補間で、Collisionの面とは最大で数十cmずれる場合があります（格子内で高さが大きくねじれたセル）。
- **中心傾斜は対称な地形で0になる**: 上の[中心傾斜と局所最大傾斜](#中心傾斜と局所最大傾斜)を参照してください。
- **未実装**: 経路探索、path／rectangle brush、不可歩行領域の自動修正はありません。

全体は[制限一覧](docs/limitations.md)にあります。

### Unreal Engine側のMCPについての既知事項

公式の`StartPIE` Toolが`PIE ended before warmup completed.`というエラーを返しながら、PIE自体は開始しているケースを確認しています。本Pluginの不具合ではありませんが、併用する場合は起動Toolを無条件に再実行せず、`IsPIERunning`などで状態を確認してください。

## Development / Testing

- [開発手順](docs/development.md): 構成、Build、Automationの実行方法
- [検証要約](docs/testing.md): これまでに行った検証の範囲と結果

Automation Testは3つのsuiteがあります。

| suite | 対象 |
|---|---|
| `LandscapeMCP.V01.SafetyAndOperations` | 作成・編集・Dry-run・Undo・Validation・非対応構成の拒否 |
| `LandscapeMCP.V02.TerrainAnalysis` | 傾斜・領域の高さ・歩行可能性・量子化誤差の上限・read-only |
| `LandscapeMCP.V03.TerrainHardening` | 実三角形・近傍解析・領域の歩行可能性・Collisionの三角形分割の確認 |

Automationは新しい未保存Mapを作り、Undo履歴も使います。作業中のEditorではなく、検証用の別Projectで実行してください。

検証はAutomationと、MCP経由の簡易E2Eが中心です。Characterを実際に走らせた検証はv0.1の範囲で行ったもので、v0.2以降の解析Toolの結果と実走行の対応は、利用する環境で確認してください。

## License

Licensed under the Apache License 2.0. 全文は[LICENSE](LICENSE)にあります。

Copyright 2026 X-RAY-1

Unreal Engineおよび公式Pluginは本リポジトリに含まれず、それぞれの提供元の規約に従います。
