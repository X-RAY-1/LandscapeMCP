# Changelog

## v0.3.0 — 2026-10-06

最初のPublic Release。Apache License 2.0で公開します。

このリリースで揃っているもの:

- **Landscapeの作成と編集**: `CreateLandscape`、`SculptRegion`、`SmoothRegion`、`FlattenRegion`
- **地形の解析**: `GetHeight`、`GetHeightRegion`（矩形領域の高さと集計）
- **傾斜と近傍の解析**: `GetSlope`（中心傾斜）、`AnalyzeSlopeNeighborhood`（周囲の実三角形の局所最大・平均傾斜）
- **歩行可能性の評価**: `EvaluateWalkability`（1点）、`EvaluateWalkabilityRegion`（領域）。歩行可能角は呼び出し側が渡すgeometry評価
- **Safety**: 完全Object Pathによる対象指定、書き込みToolのDry-run（既定）、Transaction／Undo、保存APIを呼ばない、解析Toolはread-only、PIE／Save／GC中の拒否、Sample上限、非対応Landscapeの拒否
- **Automation**: `LandscapeMCP.V01.SafetyAndOperations`、`LandscapeMCP.V02.TerrainAnalysis`、`LandscapeMCP.V03.TerrainHardening`

検証済みの環境はUnreal Engine 5.8 / Windows 64-bitです。制限は`docs/limitations.md`を参照してください。

公開準備での変更（機能の変更なし）:

- `LICENSE`（Apache License 2.0）を追加
- READMEを公開利用者向けに再構成（Requirements、Installation、MCPからの利用方法、Safety design、Limitations）
- Build済みパッケージへ`LICENSE`、`README.md`、`CHANGELOG.md`、`docs`を含めるよう`Config/FilterPlugin.ini`を設定
- Plugin descriptorへ`CreatedBy`を追加
- docsから公開前の運用メモを整理

v0.2からの機能追加:

- AnalyzeSlopeNeighborhood追加: 中心傾斜（GetSlopeと同じ中心差分）と、周囲のLandscape実三角形から求めた局所最大・平均傾斜を返す。頂点・尾根・谷底・鞍部のように中心傾斜が0になる場所でも、周囲の急斜面を検出できる
- EvaluateWalkabilityRegion追加: 矩形領域の実三角形を歩行可能角に対して一括評価し、WALKABLE / NEAR_LIMIT / UNWALKABLEの数、比率、最大傾斜、最悪の位置、領域の分類（WALKABLE / MIXED / UNWALKABLE）を返す
- GetHeightRegionへ実三角形の集計を追加: `bTriangleSlopeValid`、`maxTriangleSlopeDegrees`、`maxTriangleSlopeLocation`、`triangleCount`。4隅が[0,100,100,0]のように平均gradientが0になるセルでも、実三角形の急斜面を検出できる
- 三角形分割はUE5.8の描画・Collisionと同じ対角線00-11。Engineソースと、Collisionのline traceによる実測（Automationにも追加）で確認
- 計算層へ三角形の傾斜・集計・領域分類を追加。`slopeUncertaintyDegrees`の式を、勾配誤差を直接受け取る形でも使えるよう分離（式は無変更）
- Automation Test `LandscapeMCP.V03.TerrainHardening`追加。uncertaintyの連続誤差領域（g < e、真の勾配が0になるケース、最小・最大ノルムの境界）を直接検証
- docsで中心傾斜と局所最大傾斜の違い、`NEAR_LIMIT`が量子化誤差だけに対する測定上の区分であることを明記

互換性:

- 既存8 Toolの入力schema、挙動、安全境界、エラー文字列は変更なし
- `GetSlope`の中心差分、`GetHeightRegion.maxSlopeDegrees`（4隅平均gradient）、`slopeUncertaintyDegrees`の式は定義を変えていない
- `GetHeightRegion`の出力schemaは4 fieldの追加だけ（追加のみ、既存fieldの意味は不変）。v0.2で成功していた入力は引き続き成功する
- 新2 Toolは読み取り専用。Transaction・Modify・Package dirty化なし
- Toolset Versionを`0.2`から`0.3`へ、Plugin descriptorの`Version`を3、`VersionName`を`0.3`へ変更。Tool数は8から10へ増加
- v0.2のAutomation `LandscapeMCP.V02.TerrainAnalysis`にあったToolset Version `0.2`の完全一致確認は、「0.2以降」の確認へ変更（Versionを上げると必ず失敗するため）。完全一致はV03で確認する

## v0.2.0 — 2026-10-06

地形評価機能を追加。

- GetSlope追加: 指定World XYの傾斜角・最大上昇方向・法線を取得
- GetHeightRegion追加: 矩形領域の高度を格子で取得し、最小・最大・平均・最大傾斜セルを集計
- EvaluateWalkability追加: 呼び出し側が渡した歩行可能角に対するLandscape形状の評価
- 新3 Toolは読み取り専用。Transaction・Modify・Package dirty化なし
- Landscape端ではclipせずFAIL。Sample数・距離・角度のValidationを追加
- 計算層`LandscapeMCPTerrainAnalysis`を追加し、MCP facade・Landscape読み取りから分離
- Automation Test `LandscapeMCP.V02.TerrainAnalysis`追加
- README / docsを更新
- レビュー修正: `slopeUncertaintyDegrees`を一次近似`e/(1+g²)`から、`atan`の差を両側で評価する保守的な上限へ変更。量子化だけで歩行可能性の判定が反転し得るケースが`NEAR_LIMIT`から漏れる問題を修正。schemaとfield名は変更なし

互換性:

- 既存5 Tool（CreateLandscape / GetHeight / SculptRegion / SmoothRegion / FlattenRegion）のschema、挙動、安全境界、エラー文字列は変更なし。`LandscapeMCPOperations.cpp`の既存部分は`#include`の追加だけで、既存関数の本体は無変更
- Toolset Versionを`0.1`から`0.2`へ変更。`describe_toolset`の`version`とToolset説明文が変わります。Tool名・Toolset名は同じなので、名前で呼び出す既存クライアントに影響はありません。Versionの完全一致を確認しているクライアントだけ更新が必要です
- Plugin descriptorの`Version`を2、`VersionName`を`0.2`へ変更
- Tool数は5から8へ増加

## v0.1.0 — 2026-10-06

- Landscape MCP Toolset初版を独立リポジトリのbaselineとして整理
- CreateLandscape追加
- GetHeight追加
- SculptRegion追加
- SmoothRegion追加
- FlattenRegion追加
- Dry-run対応
- Validation対応
- Undo / Transaction対応
- Landscape Collision更新
- Automation Test追加
- MCP E2E検証
- 日本語README / docs / コードコメントを整備

Freeze版のロジック、API識別子、schema field、Toolset Version `0.1`は保持しています。PR Merge前のTag / GitHub Releaseは作成していません。
