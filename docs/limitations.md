# v0.3の制限

## 意図的なFAIL

World Partition、external actor Level、Streaming Proxy、Landscape subclass、同一GUIDの追加Proxy、非表示／ロックLevel、回転Landscape、負Scale、異常Scale、Naniteは拒否します。

Edit Layerは標準Layerが1つだけ必要です。複数Layer、非表示／ロックLayer、height alphaが1以外、Blueprint／procedural brushは非対応です。付属Foliage、Visibility Hole、欠損Component、外部／共有／巨大Heightmapも拒否します。通常読み取りでも同じ条件を検証します。

PIE／Save／GC中の操作、nested write transaction、入力・作成・編集Sample上限超過、Heightfield overflowはFAILです。

## 未実装

- path探索（FindWalkablePath）、不可歩行領域の自動Smooth、Path Flatten
- path／rectangle brush
- 複数Edit Layerの明示選択
- Heightmap適用Tool
- 独立RebuildCollision Tool
- World Partition／Proxy対応
- `NEAR_LIMIT`／`slopeUncertaintyDegrees`の名称見直し（schema互換性のため保留）

## 地形評価Toolの制限

`EvaluateWalkability`と`EvaluateWalkabilityRegion`は**Landscape形状のgeometry評価であり、CharacterMovementの完全な再現ではありません**。比較するのは傾斜角（前者は元Heightfieldのbilinear面の中心差分、後者は実三角形ごとの傾斜）と、呼び出し側が渡した`walkableFloorAngleDeg`だけです。

- 特定のCharacter、Blueprint、CharacterMovementComponentを参照しません。Walkable Floor Angleは呼び出し側が調べて渡します。
- CharacterMovementは接触したCollision三角形の法線、Capsuleの形状、Step Height、Perch、速度、Walkable Slope Overrideなどで判定します。本Toolはこれらを扱いません。
- 傾斜は`sampleDistanceCm`の幅で平均した値です。幅より細かい段差や折れ目は平均化され、Collision三角形面との差もあります。
- `WALKABLE`は歩行できることの保証ではなく、`UNWALKABLE`は進入できないことの保証でもありません。判定は実走行で確認してください。
- `NEAR_LIMIT`は、高さの量子化誤差に対する測定上の区分です。Collision三角形との差、Capsule、Step Height、Perch、その他のCharacterMovementの挙動による不確実性は含みません。
- 中心傾斜（`GetSlope`、`EvaluateWalkability`）は対称な地形で打ち消し合います。丘の頂点、尾根、谷底、鞍部では両側が急でも傾斜0・`WALKABLE`になります。これは定義どおりの挙動で、v0.3でも変えていません。周囲の急な面は`AnalyzeSlopeNeighborhood`の`localMaxSlopeDegrees`、`GetHeightRegion`の`maxTriangleSlopeDegrees`、`EvaluateWalkabilityRegion`で確認してください。
- 局所最大傾斜と`EvaluateWalkabilityRegion`は実三角形1枚ごとの評価です。Characterの足元より小さい急な三角形が1枚あるだけで最大値は大きくなり、領域は`MIXED`や`UNWALKABLE`になります。実際に通れるかどうかはCapsuleの大きさやStep Heightに依存します。
- 実三角形の分割（対角線00-11）はUE5.8で確認したものです。別のEngineバージョンでは、Automationのline trace確認で検出できます。
- 実三角形の評価はCollisionのLODや簡易Collision（Simple Collision）を考慮しません。Collisionの解像度をHeightfieldより粗くしたLandscapeでは、実際の接触面と一致しません。
- 1点の評価です。経路全体の評価は、呼び出し側が複数点または`GetHeightRegion`で行います。

`GetSlope`と`EvaluateWalkability`は、中心から`sampleDistanceCm`離れた4点すべてがLandscape内にない場合FAILします。端では片側差分へ切り替えません。そのためLandscapeの縁から`sampleDistanceCm`未満の帯は計測できません。

`AnalyzeSlopeNeighborhood`と`EvaluateWalkabilityRegion`は1回あたり最大16384頂点です。`EvaluateWalkabilityRegion`は矩形をclipせず、少しでも範囲外ならFAILします。`AnalyzeSlopeNeighborhood`は中心傾斜の4点が範囲外ならFAILしますが、半径だけは端で切ります。

`GetHeightRegion`は1回あたり最大1024 Sampleです。v0.1の最大Landscape（262144 Sample）を格子解像度で一度に読むことはできず、領域の分割か間隔の拡大が必要です。矩形はclipされず、少しでも範囲外ならFAILします。`maxSlopeDegrees`は指定間隔のセル単位の値で、間隔より細かい地形は反映しません。

評価Toolはすべて元Heightfieldを読むため、v0.1の`GetHeight`と同じ対象条件（単一標準Edit Layerなど）を満たさないLandscapeでは使えません。PIE／Save／GC中も拒否します。

GetHeightは元Heightfieldのbilinear値であり、Collision高さそのものではありません。編集成功は歩行可能性を保証しません。急斜面へのCharacter前進阻止は正常なCharacterMovement動作となる場合があります。

## 外部制約

公式StartPIEが`PIE ended before warmup completed.`と返した直後にPIEが起動済みのケースを確認しました。LandscapeMCPの不具合として分類していません。状態を確認し、無条件retryを避けます。

UE5.8／Windows 11／Win64 Editor以外は未検証です。検証に使用したVisual Studio 14.51 toolchainは、Engine推奨の14.50より新しいというBuild警告が出ていました。
