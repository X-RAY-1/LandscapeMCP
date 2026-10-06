# Tool仕様

Toolset: `LandscapeMCP.LandscapeMCPToolset`、Version: `0.2`。v0.2は既存5 Toolの入力・出力schemaを変更せず、読み取り専用の`GetSlope`／`GetHeightRegion`／`EvaluateWalkability`を追加します。公式`describe_toolset`で現在のschemaを取得できます。ここではMCP側のlower camel case field名を記載します。C++引数・UPROPERTY名は元の識別子を保持しています。

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

## 地形評価Tool（v0.2）の共通事項

`GetSlope`、`GetHeightRegion`、`EvaluateWalkability`は読み取り専用です。`bDryRun`引数はなく、Heightfield・Package・Undo履歴を変更しません。対象条件とPIE／Save／GC中の拒否は`GetHeight`と同じです。

3 Toolはそれぞれ専用の結果を返します。上の共通結果（`FLandscapeMCPResult`）とは別の構造で、既存5 Toolの出力は変わりません。角度は度、距離と高さはワールドcmです。

高さは`GetHeight`と同じく、単一標準Edit Layerの元Heightfieldをbilinear補間した値です。Collisionの面や法線ではありません。

## GetSlope

| 入力 | 型 / 制約 |
|---|---|
| `landscapePath` | string、読み込み済みActorの完全Object Path |
| `worldX` / `worldY` | number、Landscape内のWorld XY |
| `sampleDistanceCm` | number、[1,5000]。中心から±X／±Yへ離す距離 |

中心から`sampleDistanceCm`離れた4点（±X、±Y）の高さを読み、中心差分で2D勾配を求めます。

```
gx = (h(x+d, y) - h(x-d, y)) / (2d)
gy = (h(x, y+d) - h(x, y-d)) / (2d)
slopeDegrees          = atan(sqrt(gx² + gy²))
slopeDirectionDegrees = atan2(gy, gx)        （[0,360)へ正規化）
normal                = normalize(-gx, -gy, 1)
```

位置と高さはどちらもワールドcmなので、LandscapeのScale（X／Y／Zが異なる場合を含む）は勾配へそのまま反映されます。

| 出力 | 型 | 意味 |
|---|---|---|
| `bSuccess` | boolean | 成功判定 |
| `landscapePath` | string | 対象の完全Path |
| `message` | string | 結果の説明 |
| `worldX` / `worldY` | number | 入力位置 |
| `slopeDegrees` | number | 傾斜角。0は水平、90は垂直 |
| `slopeDirectionDegrees` | number | 最大上昇方向。+Xが0、+Yが90、[0,360) |
| `bDirectionValid` | boolean | 勾配がほぼ0（水平）の場合false。その場合`slopeDirectionDegrees`は0で意味を持たない |
| `normal` | `{x,y,z}` | 上向きの単位法線 |
| `sampleDistanceCm` | number | 入力値 |
| `heightCenterCm` | number | 中心の高さ。`GetHeight`と同じ値 |
| `slopeUncertaintyDegrees` | number | 高さ量子化が傾斜角へ与え得る最大誤差 |
| `heightQuantizationCm` | number | 高度量子化刻み、Scale.Z / 128 |

`sampleDistanceCm`の目安はLandscapeの格子間隔（Scale.X／Scale.Y）以上です。小さくすると局所的な面の傾き、大きくすると広い範囲の平均的な傾きになります。Characterの足元を見るならCapsule半径〜格子間隔程度を使ってください。

4点のうち1つでもLandscape外ならFAILします。端で片側差分へ切り替えると傾斜の定義が変わるためです。縁から`sampleDistanceCm`未満の位置は計測できません。

`slopeUncertaintyDegrees`は、高さ量子化による傾斜角誤差の保守的な上限です。各高さの量子化誤差は`heightQuantizationCm / 2`以下なので、軸ごとの勾配誤差は`q / (2d)`以下、2軸合成で√2倍になります。勾配の大きさ`g`は`[max(0, g - e), g + e]`に収まり、`atan`は単調なので、両側の角度差の大きい方が上限です。

```
e = sqrt(2) * heightQuantizationCm / (2 * sampleDistanceCm)
g = hypot(gx, gy)
upper = atan(g + e) - atan(g)
lower = atan(g) - atan(max(0, g - e))
slopeUncertaintyDegrees = degrees(max(upper, lower))
```

一次近似`e / (1 + g²)`は使いません。`e`が`g`に対して小さくない場合（Scale.Zが大きい、`sampleDistanceCm`が小さい）に上限とならず、量子化だけで判定が反転し得る範囲を`NEAR_LIMIT`から漏らすためです。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","worldX":2700,"worldY":3150,"sampleDistanceCm":100}
```

## GetHeightRegion

| 入力 | 型 / 制約 |
|---|---|
| `landscapePath` | string、読み込み済みActorの完全Object Path |
| `minX` / `minY` / `maxX` / `maxY` | number、World XYの矩形。`max >= min`。矩形全体がLandscape内 |
| `sampleSpacingCm` | number、[1,5000]。格子の間隔 |

`(minX, minY)`から`sampleSpacingCm`間隔で、`max`を超えない範囲をサンプルします。各軸のSample数は`floor((max - min) / sampleSpacingCm) + 1`です。間隔が割り切れない場合、最後のサンプルは`max`の内側になります（`worldMax`が実際の位置）。`min == max`の軸は1 Sampleで、線や1点の取得に使えます。

1回あたり最大**1024 Sample**（例: 32×32）。超える場合はFAILするので、間隔を広げるか領域を分割してください。上限はAI Agentが1回の応答として扱える大きさに合わせています。

| 出力 | 型 | 意味 |
|---|---|---|
| `bSuccess` | boolean | 成功判定 |
| `landscapePath` | string | 対象の完全Path |
| `message` | string | 結果の説明 |
| `sampleCount` | integer | 総Sample数。`sampleCountX * sampleCountY` |
| `sampleCountX` / `sampleCountY` | integer | 各軸のSample数 |
| `sampleSpacingCm` | number | 入力値 |
| `worldMin` / `worldMax` | `{x,y,z}` | 実際にサンプルした格子のXY範囲と、最小／最大高度 |
| `minHeightCm` / `maxHeightCm` / `meanHeightCm` | number | 全Sampleの最小・最大・平均高度 |
| `minHeightLocation` / `maxHeightLocation` | `{x,y,z}` | 最小／最大高度のSample位置 |
| `bSlopeValid` | boolean | 各軸2 Sample以上あり、セル傾斜を評価できたか |
| `maxSlopeDegrees` | number | 隣接4 Sampleで作るセルごとの傾斜の最大値 |
| `maxSlopeLocation` | `{x,y,z}` | 最大傾斜セルの中心。Zは4隅の平均高度 |
| `heightsCm` | number[] | 全Sampleの高度。行優先（Yが外側、Xが内側） |
| `heightQuantizationCm` | number | 高度量子化刻み、Scale.Z / 128 |

`heightsCm`の要素`ix + iy * sampleCountX`は、位置`(worldMin.x + ix * sampleSpacingCm, worldMin.y + iy * sampleSpacingCm)`の高さです。

`maxSlopeDegrees`は領域内で急な場所を探すための集計値です。指定間隔のセル単位なので、見つかった位置は`GetSlope`や`EvaluateWalkability`で改めて評価してください。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","minX":2000,"minY":2000,"maxX":4000,"maxY":4000,"sampleSpacingCm":100}
```

この例は21×21 = 441 Sampleです。

## EvaluateWalkability

| 入力 | 型 / 制約 |
|---|---|
| `landscapePath` | string、読み込み済みActorの完全Object Path |
| `worldX` / `worldY` | number、Landscape内のWorld XY |
| `walkableFloorAngleDeg` | number、[0,90]。呼び出し側が指定する歩行可能角 |
| `sampleDistanceCm` | number、[1,5000]。`GetSlope`と同じ意味 |

`GetSlope`と同じ方法で傾斜を測り、`walkableFloorAngleDeg`と比較します。**Landscape形状の評価であり、CharacterMovementを参照・変更しません。** 特定のCharacter Blueprintから角度を読むこともありません。対象CharacterのWalkable Floor Angleは呼び出し側が調べて渡してください。

| 出力 | 型 | 意味 |
|---|---|---|
| `bSuccess` | boolean | 評価できたか（歩行可能かどうかではない） |
| `landscapePath` | string | 対象の完全Path |
| `message` | string | 結果の説明 |
| `worldX` / `worldY` | number | 入力位置 |
| `slopeDegrees` | number | 傾斜角。`GetSlope`と同じ値 |
| `slopeDirectionDegrees` / `bDirectionValid` | number / boolean | 最大上昇方向。`GetSlope`と同じ |
| `walkableFloorAngleDeg` | number | 入力値 |
| `bWalkable` | boolean | `slopeDegrees <= walkableFloorAngleDeg` |
| `marginDegrees` | number | `walkableFloorAngleDeg - slopeDegrees`。負なら超過 |
| `classification` | string | `WALKABLE` / `NEAR_LIMIT` / `UNWALKABLE` |
| `slopeUncertaintyDegrees` | number | `NEAR_LIMIT`の判定に使う幅。`GetSlope`と同じ値 |
| `sampleDistanceCm` | number | 入力値 |
| `heightCenterCm` | number | 中心の高さ |

`bWalkable`は境界値を歩行可能側に含めます。CharacterMovementが床法線のZ成分を「閾値以上なら歩行可能」と判定するのに合わせています。

`classification`は次の規則です。

| 条件 | classification |
|---|---|
| `marginDegrees > slopeUncertaintyDegrees` | `WALKABLE` |
| `abs(marginDegrees) <= slopeUncertaintyDegrees` | `NEAR_LIMIT` |
| `marginDegrees < -slopeUncertaintyDegrees` | `UNWALKABLE` |

`NEAR_LIMIT`の幅は固定値ではなく、その計測の`slopeUncertaintyDegrees`です。高さは16-bitで量子化されているため、差がこの幅以内なら量子化の丸めだけで判定が反転し得ます。その範囲を「判定できない」として区別するのが目的です。Scale(100,100,100)・`sampleDistanceCm:100`の水平付近では約0.32度です。この幅はCollision三角形との差やCharacter側の要因を含みません。安全側に余裕を取る場合は、呼び出し側で`marginDegrees`にしきい値を設けてください。`NEAR_LIMIT`でも`bWalkable`は`marginDegrees >= 0`のまま返します。

```json
{"landscapePath":"/Temp/Untitled_1.Untitled:PersistentLevel.AI_TestLandscape","worldX":2700,"worldY":3150,"walkableFloorAngleDeg":44.77,"sampleDistanceCm":100}
```

約47度の斜面をWalkable Floor Angle 44.77度で評価すると、`bWalkable:false`、`marginDegrees`は約-2.2、`classification:"UNWALKABLE"`になります。

## 共通Validation

地形評価Toolは、`sampleDistanceCm`／`sampleSpacingCm`が[1,5000]の範囲外、`walkableFloorAngleDeg`が[0,90]の範囲外、矩形の逆転、1024 Sample超過、計測点または矩形の範囲外をFAILとします。clipは行いません。

すべてのToolは[safety](safety.md)の対象条件を満たす必要があります。座標はfiniteかつ絶対値10000000cm以下。NaN／Infinity、異常Scale、高度overflowを拒否します。ブラシ中心の範囲外はFAIL、円の縁だけの範囲外はclip。編集読み取り上限は16384 Sampleで、超えたら半径を縮める必要があります。1 Sampleも円に含まれない場合はFAILです。
