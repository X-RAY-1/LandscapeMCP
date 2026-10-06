# Tool仕様

Toolset: `LandscapeMCP.LandscapeMCPToolset`、Version: `0.1`。公式`describe_toolset`で現在のschemaを取得できます。ここではMCP側のlower camel case field名を記載します。C++引数・UPROPERTY名は元の識別子を保持しています。

## 共通

距離・高さ・座標はワールドcm。`center`は`{x,y}`、`location` / `scale`は`{x,y,z}`。`landscapePath`は読み込み済みActorの完全Object Path、`levelPath`は読み込み済みULevelの完全Object Pathです。Asset Pathやラベルは使用できません。

全書き込みToolに`bDryRun:boolean=true`があります。Dry-runは同じValidationと変更計画を実行し、対象・範囲・変更量・Sample数を返します。Heightfield・Package・Undo履歴を変更しません。本実行は同じ入力の`bDryRun:false`で行います。

成功時、公式adapterの`returnValue`に以下の共通結果を返します。

| field | 型 | 意味 |
|---|---|---|
| `bSuccess` | boolean | 成功判定 |
| `bDryRun` | boolean | Dry-runか |
| `landscapePath` | string | 対象の完全Path。作成Dry-runでは予測Path |
| `message` | string | Validation／実行結果 |
| `sampleCount` | integer | 円形範囲内Sample数。作成は全Sample、GetHeightは4 |
| `changedSampleCount` | integer | 量子化後に変わるSample数。作成では0 |
| `worldMin` / `worldMax` | `{x,y,z}` | clip後の外接矩形と結果の高度範囲 |
| `minDeltaCm` / `maxDeltaCm` | number | 変更Sampleの最小／最大高度差cm |
| `heightCm` | number | GetHeight／作成時の高度cm。領域編集の代表高度ではない |
| `heightQuantizationCm` | number | 高度量子化刻み、Scale.Z / 128 |
| `bClipped` | boolean | 領域ブラシがLandscape端でclipされたか |

領域の矩形には円外の未変更Sampleも含まれます。Smoothの読み取り上限には1 Sample幅のhaloを含みます。

失敗は操作層の`bSuccess=false`から`RaiseScriptError`で公開され、MCPは`isError=true`となります。入力schema違反も公式adapterで拒否されます。失敗後に推測で別の対象を操作しないでください。

## CreateLandscape

| 入力 | 型 / 制約 |
|---|---|
| `levelPath` | string、現在Editor Worldの可視・ロック解除済みLevel |
| `landscapeName` | string、先頭は文字、64文字以下、文字／数字／underscoreのみ。既存名／ラベル衝突を拒否 |
| `location` | Vector、XY最小隅と高度原点 |
| `scale` | Vector、各軸[1,1000]cm/座標単位 |
| `componentCountX` / `componentCountY` | integer、各[1,16]、積16以下 |
| `sectionsPerComponent` | integer、1または2。各軸のSubsection数 |
| `quadsPerSection` | integer、7 / 15 / 31 / 63 |
| `initialWorldHeight` | number、絶対World Z cm |
| `bDryRun` | boolean、既定true |

Sample寸法は各軸`componentCount * sectionsPerComponent * quadsPerSection + 1`。総Sample262144以下、各軸100000cm以下。高度は16-bit Heightfieldの表現範囲に収まることが必要です。

入力例:

```json
{"levelPath":"/Temp/Untitled_1.Untitled:PersistentLevel","landscapeName":"AI_TestLandscape","location":{"x":0,"y":0,"z":0},"scale":{"x":100,"y":100,"z":100},"componentCountX":1,"componentCountY":1,"sectionsPerComponent":1,"quadsPerSection":63,"initialWorldHeight":0,"bDryRun":true}
```

この構成は64×64 Sample、63m四方です。作成はLevelと新規ActorのTransactionを持ち、初期高度を照合します。

## GetHeight

入力は`landscapePath:string`、`worldX:number`、`worldY:number`。範囲外のXYはFAIL。単一点のみ。読み取りToolなので`bDryRun`引数はありません。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","worldX":3150,"worldY":3150}
```

単一標準Edit Layerの元Heightfieldを4 Sampleでbilinear補間し、`heightCm`を返します。Collision raycastではありません。円滑な場所でもCollision三角形面との補間差があり得ます。端では同じSampleを複数回読む場合があります。

## SculptRegion

入力: `landscapePath:string`、`center:{x,y}`、`radiusCm:number`、`strengthCm:number`、`falloff:number`、`bRaise:boolean`、`bDryRun:boolean=true`。

- Radiusは(0,5000]cm、Strengthは[0,1000]cm、Falloffは[0,1]。
- `bRaise:true`で加算、falseで減算。
- Falloffは半径のうちsmoothstepで減衰する縁の割合。0は円内で均一、1は全半径で減衰。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","center":{"x":3150,"y":3150},"radiusCm":800,"strengthCm":100,"falloff":1,"bRaise":true,"bDryRun":true}
```

## SmoothRegion

入力: `landscapePath:string`、`center:{x,y}`、`radiusCm:number`、`strength:number`、`falloff:number`、`bDryRun:boolean=true`。RadiusとFalloffはSculptと同じ、Strengthは[0,1]。

元データを固定して3×3近傍平均を1回同時適用し、Strength×ブラシ重みで補間します。UI Smoothブラシの全挙動を再現する機能ではありません。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","center":{"x":3150,"y":3150},"radiusCm":400,"strength":0.5,"falloff":1,"bDryRun":true}
```

## FlattenRegion

入力: `landscapePath:string`、`center:{x,y}`、`radiusCm:number`、`targetHeightCm:number`、`strength:number`、`falloff:number`、`bDryRun:boolean=true`。Strengthは[0,1]、Target Heightは絶対World Z cmで表現可能範囲内。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","center":{"x":3150,"y":2700},"radiusCm":220,"targetHeightCm":20,"strength":1,"falloff":0.5,"bDryRun":true}
```

長いルートは制限内の円を重ねて作れます。path／rectangle brushや、隣接境界の歩行可能性保証はありません。

## 共通Validation

すべてのToolは[safety](safety.md)の対象条件を満たす必要があります。座標はfiniteかつ絶対値10000000cm以下。NaN／Infinity、異常Scale、高度overflowを拒否します。ブラシ中心の範囲外はFAIL、円の縁だけの範囲外はclip。編集読み取り上限は16384 Sampleで、超えたら半径を縮める必要があります。1 Sampleも円に含まれない場合はFAILです。
