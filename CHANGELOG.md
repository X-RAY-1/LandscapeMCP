# Changelog

## v0.5.0 — 未リリース（RC1: 2026-11-14。公開日は公開時に記入）

Foliageが付いたLandscapeを、Foliageを壊さずに解析・編集できるようにしました。Foliageを動かす・消す・置き直すApplyはありません。Foliageに影響する編集や、影響を判断できない編集は、書き込まずに拒否します。

### Added

- `GetCapabilities`（read-only）: Engine・Adapter・Toolset・Pluginの版、Build識別、機能ごとの対応状況（Foliageの扱い、World Partition、現在のsnap mode、safety latch）。Landscapeがなくても使える
- `InspectFoliage`（read-only）: 矩形内のFoliageを種類ごとに集計し、instanceの一覧（上限4096件、既定256件）を返す。付属先、足元の地形高度、地形からの高さ、局所傾斜、判定できない形式・状態を含む
- v0.4の3 Tool（`FlattenRectangle`、`FlattenPath`、`CreateWalkablePath`）の結果へ`foliage`を追加: Foliageへの影響の予測（`UNCHANGED`、`FLOATING`、`BURIED`、`SLOPE_CHANGED`、`MAY_BE_MOVED_BY_ENGINE`、`MAY_BE_DELETED_BY_ENGINE`、`UNKNOWN`）、全体の判定（`NO_FOLIAGE`、`SAFE_TO_APPLY`、`BLOCKED`、`UNKNOWN`）、影響を受けるinstanceの例（最大64件）、Applyの事後検証（`postApply`）

### Safety

- Foliage gate: `foliage.verdict`が`NO_FOLIAGE`か`SAFE_TO_APPLY`のときだけApplyが書き込む。それ以外は`FOLIAGE_SAFETY_CONFLICT`
- Foliageの足元の高さは、Landscapeの実三角形（描画とChaos collisionと同じ分割）の重心座標補間で求める。辺・頂点の上では、接するすべての三角形の傾斜の変化を見る
- Project設定`FoliageLandscapeSnappingMode`が`DontSnap`以外で付属Foliageがある場合は拒否する（UEがTransactionの外でinstanceを動かす・消すため）。設定は変更しない
- 判定できない形式・状態は安全とみなさず拒否する: Landscape Grass、Procedural Foliage、Actor Foliage、他のLevel（非表示・ロック中を含む）のFoliage、読み込まれていないstreaming level、走査上限（65,536）の超過、付属先を証明できないinstance
- 書き込み直前に、Foliageのinstance・snap mode・判定不能な状態がgateの時点と同じことを再確認する（違えば`stage`は`preWrite`）
- 書き込み後に、遅延していたLandscapeの更新を流してから、全LevelのFoliage Actorのbyte一致と、足元の地形が予測どおりであることを検証する。失敗時のrollbackも、Foliageを含めて元に戻ったことを確認する
- 円形ブラシの3 Toolの付属Foliage判定を、非表示・ロック中のLevelと、付属先を証明できないinstanceまで広げた
- 読み込まれていないstreaming levelがあるWorldでは、円形ブラシの書き込みを拒否する
- 対象の解決で、同じLandscapeの別proxyとHeightmapの共有を、非表示Levelまで含めて確認する（v0.4.0では表示中のLevelだけを見ていた）

### Changed

- Foliageが付いたLandscapeでも、読み取りの6 Tool、`InspectFoliage`、`GetCapabilities`、3 ToolのDry-runが使える（v0.4.0では拒否していた）。読み取りはLandscapeもFoliageも変更しない
- 3 ToolのDry-runは、Foliageへの影響がある場合も計画を返し、`foliage`と警告（`FOLIAGE_<verdict>`）で知らせる。Applyは拒否される
- 平坦な地面での`CreateWalkablePath`のように書き込みが不要な場合は、Foliageがあっても`NO_CHANGE`で成功する

### Compatibility

- 既存13 Toolの入力schema、既存の出力field、`errorCode`は変更なし。追加は3 Toolの出力`foliage`と、新しい2 Toolだけ（v0.4.0のtagのschemaとの比較をAutomationで確認）
- Foliageによる拒否は既存の`FOLIAGE_SAFETY_CONFLICT`。`message`の先頭に理由（`FOLIAGE_IMPACT`、`FOLIAGE_UNKNOWN`、`FOLIAGE_SNAP_MODE`、`FOLIAGE_STALE`、`FOLIAGE_POST_VERIFY`）を付ける
- Toolset Versionを`0.4`から`0.5`へ、Plugin descriptorの`Version`を5、`VersionName`を`0.5.0`へ変更。Tool数は13から15へ増加
- Build Moduleの依存は変更なし
- Automationは60 suiteから91 suiteへ増加

### Limitations

- 円形ブラシの3 Toolは、対象Landscapeに付属していないFoliageを保護しない（付属の有無だけを見て、書き込み後のFoliageの検証をしない。v0.4からの制限）
- 付属の判定は、評価のたびに全LevelのFoliageの全instanceを走査する（上限なし）。数十万instance以上のWorldでは時間がかかる場合がある
- World Partition、Landscape Grassのあるmaterial、Procedural Foliage、Actor Foliage、他のLevelのFoliageがある範囲、読み込まれていないstreaming levelでは、地形authoringのApplyはできない
- Foliageを動かして地形に合わせる編集はない
- `GetHeight`などの読み取りToolの高さは、従来どおりbilinear補間（Foliage Safetyの高さの定義とは別）
- Landscape Grass（Grass Output・Grass Type）を使うLandscapeへの編集は非対応。円形ブラシの3 ToolはGrassの状態を検査しない（v0.3からの動作）。安全な対応は将来のバージョンで検討中で、時期は未定
- Windowsでは長いパスでBuildが失敗する場合がある。READMEのInstallationに注意を追加
- 検証済みの環境はUnreal Engine 5.8 / Windows 64-bit

詳細は`README.md`、`docs/v0.5-contract.md`、`docs/v0.5-design.md`、`docs/limitations.md`を参照してください。

## v0.4.0 — 2026-10-13

円形ブラシと解析が中心だったToolsetへ、矩形・経路・歩ける道という単位で地形を整える3 Toolを追加しました。本実行（Apply）は、書き込みの前後で地形とcollisionを検証します。

### Added

- `FlattenRectangle`: 軸平行の矩形を指定World高度へ平坦化。外側はfalloffで元の地形へつなぐ
- `FlattenPath`: Polylineに沿った帯状の領域を平坦化。高さは`fixed`／`follow_start`／`follow_path`の3方式
- `CreateWalkablePath`: 端点の高さを保ったまま、指定した最大勾配以下になる縦断形状を計画して整地。元データと予測collisionの両方で、量子化誤差を見込んだ余裕を要求する
- 3 Tool共通の結果（Terrain Operation Result）: 変更前・計画後・実測後の高さと傾斜の統計、変更頂点数、範囲、経路の縦断profile、警告、rollbackの状態
- Dry-run（既定）: 量子化後の高さまで含めた変更計画を、書き込まずに返す

### Safety

- 検証付きのApply: `bDryRun=false`は、検証を通った場合だけLandscapeへ書き込む
- 1回のApplyにつき、自分専用のEditor Transactionを1つだけ作る。Undo／Redoに対応
- 書き込み直前に、対象・地形・周辺の状態が計画時から変わっていないことを再検証する
- 書き込み後に、元データ・geometry・実際のcollisionを読み直して計画と照合する
- collisionは、UE5.8のChaos heightfieldが行う量子化を独立に計算した予測と、line traceの実測を比較する
- 検証に失敗した場合は自分のTransactionだけをUndoし、元の地形とcollisionに戻ったことを検証する
- 元に戻ったことを証明できない場合は、Editorを再起動するまで3 ToolのApplyを止める
- 保存しない。Editorの設定を変更しない。Autosave・自動checkout・Source Controlが有効な場合はApplyを拒否する
- 対応していない状態は書き込まずに拒否する（fail-closed）

### Reliability

- 同じrequestの再送: 現在の地形が記録した結果と一致する場合だけ`NO_CHANGE`を返す。地形が変わっていれば新しいrequestとして計画し直す。記録は128件まで（古いものから破棄）で、Editorの再起動で消える
- Landscapeの上下にWorldStaticのpropがあっても、対象Landscapeのcollisionまで検証を続ける。propは動かさず、読み飛ばせるのは256 componentまで
- 失敗時の診断に、拒否した処理段階を示す`stage`を追加

### Compatibility

- 既存10 Toolの入力・出力の構造、type、required、defaultは変更なし
- 説明文だけの変更: Toolsetの説明と、`GetHeightRegion`の出力field `bTriangleSlopeValid`の説明から、旧version表記を削除
- 既存Toolと共通の対象解決エラーのうち3つの文言から、旧version表記を削除（`…unsupported in v0.1.` → `…unsupported.`、`…the v0.1 total sample limit.` → `…the supported total sample limit.`）。エラーの条件は同じ
- Toolset Versionを`0.3`から`0.4`へ、Plugin descriptorの`Version`を4、`VersionName`を`0.4.0`へ変更。Tool数は10から13へ増加
- Build Moduleの依存へSourceControl、Chaos、PhysicsCoreを追加（すべてEngine付属）
- Automationは3 suiteから60 suiteへ増加

### Limitations

- 対象は読み込み済みの単一の`ALandscape`。World Partition、Streaming Proxy、回転、Nanite、Visibility Hole、複数または特殊なEdit Layer、付属Foliageは非対応（従来どおり）
- 3 ToolのApplyは、collisionがHeightfieldと同じ解像度で、materialがEngineのdefault material（Landscape Grassなし）のLandscapeだけ
- Dry-runの成功はApplyの受理を保証しない。ApplyだけがEditorとcollisionの状態を追加で検査する
- `FlattenRectangle`の回転（`yawDegrees`）は0だけ。経路探索はしない
- 記録が消えた後の同じrequestは新しい編集として実行される（永久に冪等ではない）
- `CreateWalkablePath`の保証はLandscapeの形状についてのもので、CharacterMovementやNavMeshでの通行は保証しない
- 検証済みの環境はUnreal Engine 5.8 / Windows 64-bit

詳細は`README.md`、`docs/v0.4-tools.md`、`docs/v0.4-apply.md`、`docs/limitations.md`を参照してください。

## v0.3.0 — 2026-10-06

最初のPublic Release。Apache License 2.0で、ソースのみを公開します。Build済みのBinariesは配布しません。

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
- `RunUAT BuildPlugin`の出力へ`LICENSE`、`README.md`、`CHANGELOG.md`、`docs`が含まれるよう`Config/FilterPlugin.ini`を設定
- Plugin descriptorへ`CreatedBy`を追加し、`VersionName`を`0.3`から`0.3.0`へ変更（Toolset Versionは`0.3`のまま。`IsExperimentalVersion`は`true`のまま）
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
