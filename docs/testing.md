# 検証要約

## FreezeまでのE2E（2026-10-05〜2026-10-06）

判定: **LandscapeMCP v0.1: FREEZE CANDIDATE**。

| 確認内容 | 結果 |
|---|---|
| MCP検出 | 検証環境で53 Toolsets、LandscapeMCP 5 Tool検出・実行 |
| Dry-run | 27回PASS、書き込みなし |
| Undo | 高度変更をUndoで復元し、再実行で戻した |
| Landscape作成・編集 | 未保存63m四方でCreate、丘、窪地、Smooth、Flattenを確認 |
| 高度／Collision | 6代表点で最大差約1.90cm。Source bilinearとCollision三角形面の差を考慮 |
| Goal完走 | 通常CharacterMovement入力で到達。境界往復を含むXY総走行約80.05m |
| Boundary | Flatten境界・ルート端への接近と逆入力復帰PASS |
| 急斜面 | EXPECTED_UNWALKABLE_SLOPEへ分類 |
| Collision | すり抜け・Capsule埋まり・持続Fallingなし |
| Cleanup | callback、保留入力、fixture解除、StopPIE、IsPIERunning=false |
| Hash／保存 | Content276件、Plugin51件のSHA256一致。Level／Asset保存なし |

53という総数は当時のProjectで有効だった他Pluginを含む数であり、本Plugin導入後の普遍的な総数ではありません。

## 急斜面の切り分け

前回のPHYSICAL_STUCKを同じ丘へ通常入力で再現し、前方Collision面47.16°がCharacter WalkableFloorAngle44.77°を超えることを確認しました。World game-timeは前進阻止中にも3秒進行。逆方向入力後に平坦部へ約4.03秒で復帰できました。

最終試験は77 sampleすべてMOVE_WALKING、XY積算19.58m、wall18.718秒に対しWorld18.747秒。すり抜け・埋まり・Fallingなし。前進のみ阻止され、復帰不能ではないため、LandscapeMCPのFAIL条件から除外しました。

前のBoundary走行では短いMOVE_FALLINGが1 sampleあり、約0.25秒後にWALKINGへ戻っています。全過去試験でFallingが一度もなかったという意味ではありません。

## 公式StartPIEの応答不整合

StartPIEがisError=true／`PIE ended before warmup completed.`を返しても、IsPIERunning=true、PIE World、PlayerController、Characterが存在するケースを繰り返し確認しました。起動済み・応答不整合として扱い、自動retryしません。LandscapeMCPの不具合とは分類していません。

## v0.1.0整理後の再検証

2026-10-06にリポジトリSourceからRunUAT BuildPlugin（Win64）を再実行し、成功・終了コード0を確認しました。全7 Sourceファイルについてコメントを除いたコードがFreeze版と一致し、Plugin descriptorはbyte一致しています。コード識別子・実行文字列・schema fieldの変更なし。コメント日本語化によりUHTのTool説明文は日本語になります。

Build済みPluginを既存Projectとは別の隔離Projectへ配置し、Automation suite `LandscapeMCP.V01.SafetyAndOperations` を再実行してPASSしました。成功1、失敗0、警告付き成功0、未実行0、約0.372秒、Editor終了コード0。公式生成schemaもFreeze時のschemaとdescriptionを除いて一致しています。

Automationの対象は、無効対象、作成構成、Radius／Strength／Falloff、NaN／Infinity、範囲外、GetHeight、Sculpt、Smoothの局所差縮小、Flattenの目標接近、clip、ロック／回転、Texture共有拒否、Undo、Dry-runのPackage／Undo非変更、他Actor保持、schema生成、Level保存イベント0です。

ログ、raw E2E JSON、Screenshot、Automation report、hash一覧、session log、Temp Levelはリポジトリに含めません。検証要約だけを管理します。機能変更・再編集の実機E2Eは今回再実行していません。

## v0.2 地形評価Toolの検証（2026-10-06）

対象は`feature/v0.2-terrain-analysis`。Codexレビュー前の結果です。Characterの実走行、長時間PIE、Boundary検証は行っていません。

### Build

リポジトリSourceからRunUAT BuildPlugin（Win64、UE5.8）を実行し、`BUILD SUCCESSFUL`・終了コード0。警告はEngineヘッダー由来の非推奨API警告だけで、本Pluginのコードに起因する警告・エラーはありません。

### Automation

Build済みPluginを、`ToolsetRegistry`と本Pluginだけを有効にした隔離Project（`ModelContextProtocol`は無効）へ配置し、`Automation RunTests LandscapeMCP`を実行しました。

| suite | 結果 |
|---|---|
| `LandscapeMCP.V01.SafetyAndOperations`（v0.1 regression、無変更） | Success、エラー0、警告0 |
| `LandscapeMCP.V02.TerrainAnalysis`（新規） | Success、エラー0、警告0 |

成功2、失敗0、警告付き成功0、未実行0、Editor終了コード0。

`V02.TerrainAnalysis`の対象: 計算層単体（傾斜・方向・法線・歩行可能性・領域集計）、平坦地の傾斜0、既知の斜面（勾配0.5 → 26.565度）、上り／下り／+Y／斜めの方向、計測位置と距離を変えた場合の一致、端での拒否、`sampleDistanceCm`の範囲外・NaN・Infinity、座標のNaN・Infinity、GetHeightRegionのSample数・最小／最大／平均・行優先の並び・GetHeightとの一致・割り切れない間隔・1点／1行・上限ちょうど（1024）・上限超過・逆転・範囲外・異常な間隔、Walkabilityの閾値未満／超過／一致／量子化誤差内、角度の範囲外、約47度の斜面を44.77度で`UNWALKABLE`と判定、読み取りがPackageをdirtyにしない・Undo履歴を増やさない、他Actorの保持、保存イベント0、ロックLayer／回転Landscapeの拒否、schemaに8 Toolが含まれること、Toolset Version `0.2`。

斜面のfixtureはテストコード内でHeightfieldを平面へ直接設定しています。量子化誤差なしで表現できる値を使い、許容差は1e-6としました。

### schema互換性

v0.1稼働中の`describe_toolset`と、v0.2の公式生成schemaを比較しました。既存5 Toolは`description`（コメントの言語）を除き、入力・出力schemaが完全一致です。追加は`GetSlope`、`GetHeightRegion`、`EvaluateWalkability`の3つ、Toolset Versionは`0.1`→`0.2`です。

### MCP簡易E2E（Claude Code）

v0.2のBinariesでEditorを起動し、登録済みの公式MCPから`call_tool`で実行しました。空の未保存Level（`/Temp/Untitled_0`）に63m四方のLandscapeを作成し、中心(3000,3000)・半径600cm・高さ600cmの丘をSculpt（いずれもDry-run後に本実行）。保存はしていません。

| 確認内容 | 結果 |
|---|---|
| Toolset検出 | Version `0.2`、8 Tool |
| 平坦地 | 傾斜0、`bDirectionValid:false`、法線(0,0,1) |
| 丘の斜面4点 | 傾斜55.05〜55.32度。GetHeightの値から手計算した中心差分と一致 |
| 最大上昇方向 | 丘の東側で180、北側で270、西側で0、北東側で225。いずれも丘の中心向き |
| `heightCenterCm` | 5点すべてGetHeightと一致 |
| EvaluateWalkability（44.77度） | 斜面4点は`bWalkable:false`・`UNWALKABLE`・margin約-10.3〜-10.6。傾斜はGetSlopeと一致 |
| EvaluateWalkability（60度） | 同じ斜面が`bWalkable:true`・`WALKABLE`・margin約4.68 |
| GetHeightRegion（2400〜3600、100cm間隔） | 13×13 = 169 Sample。最小0・最大600・平均120.5067が`heightsCm`からの再計算と一致。最大高度の位置は丘の中心 |
| `heightsCm`の並び | 3点をGetHeightと照合して一致 |
| `maxSlopeDegrees` | 55.84度（セル単位） |
| 端での計測 | FAIL（clipなし） |
| 1024 Sample超過、逆転した範囲、角度91、名前だけのPath | すべてFAIL |

丘の頂点(3000,3000)は傾斜0・`WALKABLE`と評価されました。中心差分が対称な地形で打ち消し合うためで、[制限](limitations.md)に記載しています。

稼働中Editorの「新規レベル」はモーダルダイアログを開き、その間MCP呼び出しが止まるため、MCPからは操作できませんでした。空の未保存Levelは、起動Mapを空にする起動オプション（`-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:EditorStartupMap=`）でEditorを起動して用意しています。Projectの設定ファイルは変更していません。

## v0.2 レビュー修正後の再検証（2026-10-06）

Codexレビュー（`REQUEST_CHANGES`、P2）を受け、`slopeUncertaintyDegrees`を一次近似`e / (1 + g²)`から、`atan`の差を両側で評価する保守的な上限へ変更しました。変更はこの計算式とテスト・docsだけで、`NEAR_LIMIT`の意味、field名、schema、Safetyは変えていません。

| 項目 | 結果 |
|---|---|
| Build（RunUAT BuildPlugin、Win64） | `BUILD SUCCESSFUL`、終了コード0 |
| `LandscapeMCP.V01.SafetyAndOperations` | Success、エラー0、警告0 |
| `LandscapeMCP.V02.TerrainAnalysis` | Success、エラー0、警告0 |
| schema | 全8 Toolが修正前のv0.2とdescriptionを含め完全一致 |

追加したAutomationの対象:

- 計算層の11ケース（平坦、最小distance、斜め、急斜面、勾配が誤差より小さい場合、Scale.Zの最大・最小など）で、仕様の式と一致すること、4点の高さが±q/2ずれる全9通りの傾斜角のずれを上回ること
- レビューの反例（Scale.Z=100、distance 1cm、斜め45度）: 量子化だけで約24.1度まで下がり得るのに対し、一次近似の幅は約15.8度で上限にならないこと、修正後の幅（約20.9度）が上限になること
- 閾値反転: 上の反例を歩行可能角27度で評価すると、一次近似では`UNWALKABLE`と断定されていたものが`NEAR_LIMIT`になること
- 平坦側: 公称0度・distance 1cmで幅が約28.9度になり、歩行可能角20度は`NEAR_LIMIT`、40度は`WALKABLE`
- 実Landscape Scale(1,1,100)・distance 1cm: 平坦と斜め（約47.85度）の両方。歩行可能角30度が`NEAR_LIMIT`、10度が`UNWALKABLE`、80度が`WALKABLE`
- 実Landscape Scale(200,50,25)の非等方: 傾斜・方向・幅が式と一致。distanceを50cmから1cmへ縮めると幅が広がること
- Scale(100,100,100)の約46.9度の斜面: 歩行可能角40度でdistance 100cmは`UNWALKABLE`、distance 1cmは`NEAR_LIMIT`

MCP簡易E2E（Claude Code）は修正版Binariesで再実行しました。空の未保存Levelに前回と同じLandscapeと丘を作成（Dry-run後に本実行）。保存はしていません。

| 確認内容 | 結果 |
|---|---|
| 傾斜・方向・`heightCenterCm` | 7ケースすべてGetHeightからの手計算と一致（前回と同じ値） |
| `slopeUncertaintyDegrees` | 7ケースすべて仕様の式と一致 |
| distance 100cm | 平坦0.3165度、斜面約0.103度。一次近似との差は0.001度未満 |
| distance 1cm・平坦 | 28.92度（一次近似は31.65度） |
| distance 1cm・約55度の斜面 | 13.56〜13.83度（一次近似は10.25〜10.44度で過小） |
| 閾値反転（約54.95度、distance 1cm、歩行可能角43度） | margin -11.95、`NEAR_LIMIT`。一次近似の幅10.44度では`UNWALKABLE`になっていたケース |
| 明確なケース | distance 100cmの44.77度は`UNWALKABLE`、60度は`WALKABLE`で前回と同じ |
| GetHeightRegion | 169 Sample、最小0・最大600・平均120.5067で前回と同じ |
| 端での計測 | FAIL（clipなし） |

`NEAR_LIMIT`／`slopeUncertaintyDegrees`の名称見直しと`localMaxSlopeDegrees`はnon-blockingの指摘で、v0.3候補として[制限](limitations.md)に記載しました。
