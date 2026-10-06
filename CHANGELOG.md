# Changelog

## v0.2.0 — 未リリース（レビュー前）

地形評価機能を追加。`feature/v0.2-terrain-analysis`で作業中で、`main`へは未Mergeです。

- GetSlope追加: 指定World XYの傾斜角・最大上昇方向・法線を取得
- GetHeightRegion追加: 矩形領域の高度を格子で取得し、最小・最大・平均・最大傾斜セルを集計
- EvaluateWalkability追加: 呼び出し側が渡した歩行可能角に対するLandscape形状の評価
- 新3 Toolは読み取り専用。Transaction・Modify・Package dirty化なし
- Landscape端ではclipせずFAIL。Sample数・距離・角度のValidationを追加
- 計算層`LandscapeMCPTerrainAnalysis`を追加し、MCP facade・Landscape読み取りから分離
- Automation Test `LandscapeMCP.V02.TerrainAnalysis`追加
- README / docsを更新

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
