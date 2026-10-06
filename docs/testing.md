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
