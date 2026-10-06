# v0.2の制限

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
- v0.3候補: `NEAR_LIMIT`／`slopeUncertaintyDegrees`の名称見直し、`sampleDistanceCm`の範囲内の局所最大傾斜（`localMaxSlopeDegrees`）。頂点・尾根での打ち消しへの対処を含みます

## 地形評価Tool（v0.2）の制限

`EvaluateWalkability`は**Landscape形状のgeometry評価であり、CharacterMovementの完全な再現ではありません**。比較するのは「元Heightfieldのbilinear面を中心差分で測った傾斜角」と「呼び出し側が渡した`walkableFloorAngleDeg`」だけです。

- 特定のCharacter、Blueprint、CharacterMovementComponentを参照しません。Walkable Floor Angleは呼び出し側が調べて渡します。
- CharacterMovementは接触したCollision三角形の法線、Capsuleの形状、Step Height、Perch、速度、Walkable Slope Overrideなどで判定します。本Toolはこれらを扱いません。
- 傾斜は`sampleDistanceCm`の幅で平均した値です。幅より細かい段差や折れ目は平均化され、Collision三角形面との差もあります。
- `WALKABLE`は歩行できることの保証ではなく、`UNWALKABLE`は進入できないことの保証でもありません。判定は実走行で確認してください。
- `NEAR_LIMIT`は高さ量子化だけを考慮した幅です。Collisionとの差やCharacter側の要因は含みません。
- 中心差分は対称な地形で打ち消し合います。丘の頂点、尾根、谷底では両側が急でも傾斜0・`WALKABLE`になります（簡易E2Eで、周囲が約55度の丘の頂点が0度と評価されることを確認）。頂点や尾根の周辺は、周囲の点または`GetHeightRegion`の`maxSlopeDegrees`で確認してください。
- 1点の評価です。経路全体の評価は、呼び出し側が複数点または`GetHeightRegion`で行います。

`GetSlope`と`EvaluateWalkability`は、中心から`sampleDistanceCm`離れた4点すべてがLandscape内にない場合FAILします。端では片側差分へ切り替えません。そのためLandscapeの縁から`sampleDistanceCm`未満の帯は計測できません。

`GetHeightRegion`は1回あたり最大1024 Sampleです。v0.1の最大Landscape（262144 Sample）を格子解像度で一度に読むことはできず、領域の分割か間隔の拡大が必要です。矩形はclipされず、少しでも範囲外ならFAILします。`maxSlopeDegrees`は指定間隔のセル単位の値で、間隔より細かい地形は反映しません。

3 Toolとも元Heightfieldを読むため、v0.1の`GetHeight`と同じ対象条件（単一標準Edit Layerなど）を満たさないLandscapeでは使えません。PIE／Save／GC中も拒否します。

GetHeightは元Heightfieldのbilinear値であり、Collision高さそのものではありません。編集成功は歩行可能性を保証しません。急斜面へのCharacter前進阻止は正常なCharacterMovement動作となる場合があります。

## 外部制約

公式StartPIEが`PIE ended before warmup completed.`と返した直後にPIEが起動済みのケースを確認しました。LandscapeMCPの不具合として分類していません。状態を確認し、無条件retryを避けます。

UE5.8／Windows 11／Win64 Editor以外は未検証。使用したVisual Studio 14.51 toolchainはEngine推奨14.50より新しいというBuild警告がありました。Licenseは未指定です。
