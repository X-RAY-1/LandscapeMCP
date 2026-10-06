# v0.1の制限

## 意図的なFAIL

World Partition、external actor Level、Streaming Proxy、Landscape subclass、同一GUIDの追加Proxy、非表示／ロックLevel、回転Landscape、負Scale、異常Scale、Naniteは拒否します。

Edit Layerは標準Layerが1つだけ必要です。複数Layer、非表示／ロックLayer、height alphaが1以外、Blueprint／procedural brushは非対応です。付属Foliage、Visibility Hole、欠損Component、外部／共有／巨大Heightmapも拒否します。通常読み取りでも同じ条件を検証します。

PIE／Save／GC中の操作、nested write transaction、入力・作成・編集Sample上限超過、Heightfield overflowはFAILです。

## 未実装

- batch／region height query
- slope query、walkability、path探索
- path／rectangle brush
- 複数Edit Layerの明示選択
- Heightmap適用Tool
- 独立RebuildCollision Tool
- World Partition／Proxy対応

GetHeightは元Heightfieldのbilinear値であり、Collision高さそのものではありません。編集成功は歩行可能性を保証しません。急斜面へのCharacter前進阻止は正常なCharacterMovement動作となる場合があります。

## 外部制約

公式StartPIEが`PIE ended before warmup completed.`と返した直後にPIEが起動済みのケースを確認しました。LandscapeMCPの不具合として分類していません。状態を確認し、無条件retryを避けます。

UE5.8／Windows 11／Win64 Editor以外は未検証。使用したVisual Studio 14.51 toolchainはEngine推奨14.50より新しいというBuild警告がありました。Licenseは未指定です。
